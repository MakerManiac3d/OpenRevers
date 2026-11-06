// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the app's end of the agent link (see mcp/AgentLink.h): listens
// on 127.0.0.1 only and runs what a paired bridge asks, in front of the user.
#pragma once

#include "app/CommandRunner.h"

#include <QObject>
#include <QPointer>
#include <QTcpServer>

namespace orv {

class AgentServer : public QObject {
    Q_OBJECT
public:
    AgentServer(CommandRunner& runner, QObject* parent = nullptr);
    /// Start (or restart) with the current agent settings; returns false and
    /// sets error() when the port is taken.
    bool start();
    void stop();
    bool listening() const { return server_.isListening(); }
    int port() const { return port_; }
    QString error() const { return error_; }
    int clients() const { return clients_; }

signals:
    void statusChanged();

private:
    void onConnection();
    void onLine(QTcpSocket* s, const QByteArray& line);

    CommandRunner& runner_;
    QTcpServer server_;
    AgentSettings settings_;
    int port_{0};
    int clients_{0};
    QString error_;
};

}  // namespace orv
