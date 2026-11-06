// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AgentServer.h"

#include "mcp/LocalBackend.h"

#include <QHostAddress>
#include <QTcpSocket>

namespace orv {

AgentServer::AgentServer(CommandRunner& runner, QObject* parent) : QObject(parent), runner_(runner) {
    connect(&server_, &QTcpServer::newConnection, this, &AgentServer::onConnection);
}

bool AgentServer::start() {
    stop();
    settings_ = loadAgentSettings();
    port_ = settings_.port;
    error_.clear();
    if (!settings_.enabled) {
        emit statusChanged();
        return false;
    }
    if (!server_.listen(QHostAddress::LocalHost, static_cast<quint16>(port_))) {
        error_ = server_.errorString();
        emit statusChanged();
        return false;
    }
    emit statusChanged();
    return true;
}

void AgentServer::stop() {
    if (server_.isListening()) server_.close();
    emit statusChanged();
}

void AgentServer::onConnection() {
    while (QTcpSocket* s = server_.nextPendingConnection()) {
        // Only this computer; QTcpServer listens on loopback, checked again here.
        if (!s->peerAddress().isLoopback()) {
            s->abort();
            s->deleteLater();
            continue;
        }
        ++clients_;
        emit statusChanged();
        connect(s, &QTcpSocket::readyRead, this, [this, s]() {
            while (s->canReadLine()) onLine(s, s->readLine());
            if (s->bytesAvailable() > 64 * 1024 * 1024) s->abort();  // no line that long is legitimate
        });
        connect(s, &QTcpSocket::disconnected, this, [this, s]() {
            --clients_;
            emit statusChanged();
            s->deleteLater();
        });
    }
}

void AgentServer::onLine(QTcpSocket* s, const QByteArray& line) {
    QPointer<QTcpSocket> sock(s);
    auto reply = [sock](const json& r) {
        if (sock) {
            sock->write(QByteArray::fromStdString(r.dump()) + '\n');
            sock->flush();
        }
    };
    json req;
    try {
        req = json::parse(line.toStdString());
    } catch (const json::exception&) {
        reply({{"ok", false}, {"code", "invalid_input"}, {"message", "not JSON"}});
        return;
    }
    const json id = req.value("id", json());
    if (req.value("token", std::string()) != settings_.token) {
        reply({{"id", id}, {"ok", false}, {"code", "refused"}, {"message", "wrong pairing token"}});
        s->disconnectFromHost();
        return;
    }
    const std::string op = req.value("op", "");
    const QString lang = QString::fromStdString(req.value("lang", std::string("en")));
    if (op == "hello") {
        reply({{"id", id}, {"ok", true}, {"result", {{"app", "OpenRevers"}}}});
    } else if (op == "list") {
        reply({{"id", id}, {"ok", true}, {"result", describeCommands(runner_.registry(), true)}});
    } else if (op == "call") {
        const QString name = QString::fromStdString(req.value("name", std::string()));
        runner_.submit(name, req.value("params", json::object()), "agent", PathPolicy::forAgent(settings_), lang,
                       [reply, id](bool ok, const json& result, const QString& code, const QString& message) {
                           if (ok) reply({{"id", id}, {"ok", true}, {"result", result}});
                           else reply({{"id", id}, {"ok", false}, {"code", code.toStdString()}, {"message", message.toStdString()}});
                       });
    } else {
        reply({{"id", id}, {"ok", false}, {"code", "invalid_input"}, {"message", "unknown op"}});
    }
}

}  // namespace orv
