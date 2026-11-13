// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CommandRunner.h"

#include "core/util/Cancel.h"

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
    Job job{name, params, caller, lang, paths, std::move(done)};
    // A query that changes nothing (and writes no file) answers at once, even
    // while a long command runs: the document is read under its own lock.
    const Command* c = registry_.find(name.toStdString());
    if (running_ && c && !c->mutates && c->group != "file") {
        runNow(std::move(job));
        return;
    }
    queue_.push_back(std::move(job));
    if (!running_) next();
}

void CommandRunner::cancel() {
    if (!running_) return;
    cancel_ = true;
    // Whatever that caller queued after it would act on a result that will not come.
    std::deque<Job> keep;
    for (auto& j : queue_) {
        if (j.caller == currentCaller_) {
            if (j.done) j.done(false, json(), "cancelled", tr("Cancelled"));
        } else {
            keep.push_back(std::move(j));
        }
    }
    queue_ = std::move(keep);
}

void CommandRunner::runNow(Job job) {
    auto work = [this, job]() {
        Outcome o;
        CommandContext ctx{doc_, job.paths, job.lang.toStdString(), job.caller.toStdString(), nullptr};
        CancelScope scope(&cancel_);
        try {
            o.result = registry_.run(job.name.toStdString(), job.params, ctx);
            o.ok = true;
        } catch (const Cancelled&) {
            o.code = "cancelled";
            o.message = tr("Cancelled: nothing changed");
        } catch (const CommandError& e) {
            o.code = QString::fromStdString(e.code());
            o.message = QString::fromUtf8(e.what());
        } catch (const std::exception& e) {
            o.code = "failed";
            o.message = QString::fromUtf8(e.what());
        }
        return o;
    };
    auto finish = [this, job](const Outcome& o) {
        const Command* c = registry_.find(job.name.toStdString());
        const QString title = c ? QString::fromStdString(c->title.in(job.lang.toStdString())) : job.name;
        emit completed(job.name, title, job.caller, o.ok, job.params, o.result, o.message);
        if (job.done) job.done(o.ok, o.result, o.code, o.message);
    };
    const Command* c = registry_.find(job.name.toStdString());
    if (c && c->appOnly) {
        finish(work());
        return;
    }
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [watcher, finish]() {
        finish(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(work));
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
    cancel_ = false;
    current_ = job.name;
    currentCaller_ = job.caller;
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
    current_.clear();
    next();
}

}  // namespace orv
