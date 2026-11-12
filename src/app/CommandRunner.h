// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — runs commands one at a time, the heavy ones on a worker thread
// so the window never freezes. The interface and the agent share this queue.
#pragma once

#include "commands/Command.h"

#include <QObject>
#include <QString>

#include <deque>
#include <functional>

namespace orv {

class CommandRunner : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(bool ok, const json& result, const QString& code, const QString& message)>;

    CommandRunner(CommandRegistry& registry, Document& doc, QObject* parent = nullptr);

    /// Queue a command. `done` is called on the interface thread.
    void submit(const QString& name, const json& params, const QString& caller, const PathPolicy& paths, const QString& lang, Done done = {});
    bool busy() const { return running_; }
    CommandRegistry& registry() { return registry_; }

signals:
    void started(const QString& title, const QString& caller);
    void finished(const QString& title, const QString& caller, bool ok, const QString& summary);
    void busyChanged(bool busy);
    /// Every command run, by anyone, with its input and its whole answer (or error).
    void completed(const QString& name, const QString& title, const QString& caller, bool ok, const json& params, const json& result, const QString& message);

private:
    struct Job {
        QString name;
        json params;
        QString caller, lang;
        PathPolicy paths;
        Done done;
    };
    void next();
    void complete(const Job& job, bool ok, const json& result, const QString& code, const QString& message);

    CommandRegistry& registry_;
    Document& doc_;
    std::deque<Job> queue_;
    bool running_{false};
};

}  // namespace orv
