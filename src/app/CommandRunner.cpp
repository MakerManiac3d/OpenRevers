// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CommandRunner.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace orv {

namespace {
struct Outcome {
    bool ok{false};
    json result;
    QString code, message;
};
}  // namespace

CommandRunner::CommandRunner(CommandRegistry& registry, Document& doc, QObject* parent) : QObject(parent), registry_(registry), doc_(doc) {}

void CommandRunner::submit(const QString& name, const json& params, const QString& caller, const PathPolicy& paths, const QString& lang, Done done) {
    queue_.push_back({name, params, caller, lang, paths, std::move(done)});
    if (!running_) next();
}

void CommandRunner::next() {
    if (queue_.empty()) {
        if (running_) {
            running_ = false;
            emit busyChanged(false);
        }
        return;
    }
    if (!running_) {
        running_ = true;
        emit busyChanged(true);
    }
    Job job = std::move(queue_.front());
    queue_.pop_front();
    const Command* c = registry_.find(job.name.toStdString());
    const QString lang = job.lang;
    const QString title = c ? QString::fromStdString(c->title.in(lang.toStdString())) : job.name;
    emit started(title, job.caller);

    auto work = [this, job]() {
        Outcome o;
        CommandContext ctx{doc_, job.paths, job.lang.toStdString(), job.caller.toStdString(), nullptr};
        try {
            o.result = registry_.run(job.name.toStdString(), job.params, ctx);
            o.ok = true;
        } catch (const CommandError& e) {
            o.code = QString::fromStdString(e.code());
            o.message = QString::fromUtf8(e.what());
        } catch (const std::exception& e) {
            o.code = "failed";
            o.message = QString::fromUtf8(e.what());
        }
        return o;
    };

    // Commands that touch the view run here, on the interface thread; the rest on a worker.
    if (!c || c->appOnly) {
        const Outcome o = work();
        complete(job, o.ok, o.result, o.code, o.message);
        return;
    }
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, job]() {
        const Outcome o = watcher->result();
        watcher->deleteLater();
        complete(job, o.ok, o.result, o.code, o.message);
    });
    watcher->setFuture(QtConcurrent::run(work));
}

void CommandRunner::complete(const Job& job, bool ok, const json& result, const QString& code, const QString& message) {
    const Command* c = registry_.find(job.name.toStdString());
    const QString title = c ? QString::fromStdString(c->title.in(job.lang.toStdString())) : job.name;
    QString summary = ok ? (result.is_object() && result.contains("summary") ? QString::fromStdString(result["summary"].get<std::string>()) : QString()) : message;
    emit finished(title, job.caller, ok, summary);
    emit completed(job.name, title, job.caller, ok, job.params, result, message);
    if (job.done) job.done(ok, result, code, message);
    next();
}

}  // namespace orv
