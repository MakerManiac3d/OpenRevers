// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AgentChat.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace orv {

namespace {

/// Enter sends, Shift+Enter starts a new line.
class ChatInput : public QPlainTextEdit {
public:
    std::function<void()> onSend;
protected:
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
            if (onSend) onSend();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }
};

QString cliPath() {
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef _WIN32
    const QString name = "openrevers-cli.exe";
#else
    const QString name = "openrevers-cli";
#endif
    for (const QString& d : {dir, dir + "/.."})
        if (QFileInfo::exists(d + "/" + name)) return QDir::cleanPath(d + "/" + name);
    return name;
}

QString language() {
    const QString l = QSettings().value("language", "auto").toString();
    if (l == "es" || l == "en") return l;
    return QLocale().name().startsWith("es") ? "es" : "en";
}

const char* kSystem =
    "You are working inside OpenRevers, the person's program for reverse engineering scanned parts, through its MCP tools "
    "(mcp__openrevers__*), which act on the project open on their screen: they see every change as you make it. "
    "Start with session_state, and read guide_read before the first task. When they say \"this\", \"these faces\" or a "
    "kept selection such as sel1, read view_selection (with name for a kept one). Tools that change the model take "
    "dryRun to try first. Reply in the person's language, briefly, and say what you changed.";

}  // namespace

QString AgentChat::findClaude() {
    const QString set = QSettings().value("agent/claudePath").toString();
    if (!set.isEmpty() && QFileInfo::exists(set)) return set;
    QString found = QStandardPaths::findExecutable("claude");
    if (!found.isEmpty()) return found;
    // Where Anthropic's installers put it.
    const QString home = QDir::homePath();
    QStringList extra = {home + "/.local/bin", home + "/.claude/local", "/usr/local/bin", "/opt/homebrew/bin"};
#ifdef _WIN32
    extra << home + "/AppData/Roaming/npm" << home + "/.local/bin";
#endif
    return QStandardPaths::findExecutable("claude", extra);
}

AgentChat::AgentChat(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    view_ = new QTextBrowser();
    view_->setOpenExternalLinks(true);
    v->addWidget(view_, 1);
    auto* input = new ChatInput();
    input->onSend = [this]() { send(); };
    input->setPlaceholderText(tr("Ask the agent… (Enter sends, Shift+Enter new line; Win+H dictates)"));
    input->setMaximumHeight(90);
    input_ = input;
    v->addWidget(input_);
    auto* row = new QHBoxLayout();
    withSelection_ = new QCheckBox(tr("Include the selection"));
    withSelection_->setChecked(true);
    withSelection_->setToolTip(tr("Adds what is picked now to the message, so \"round these edges\" means the ones you picked"));
    row->addWidget(withSelection_);
    row->addStretch(1);
    auto* fresh = new QPushButton(tr("New conversation"));
    connect(fresh, &QPushButton::clicked, this, &AgentChat::newConversation);
    row->addWidget(fresh);
    stopButton_ = new QPushButton(tr("Stop"));
    stopButton_->setEnabled(false);
    connect(stopButton_, &QPushButton::clicked, this, &AgentChat::stop);
    row->addWidget(stopButton_);
    sendButton_ = new QPushButton(tr("Send"));
    sendButton_->setDefault(true);
    connect(sendButton_, &QPushButton::clicked, this, &AgentChat::send);
    row->addWidget(sendButton_);
    v->addLayout(row);
    status_ = new QLabel();
    status_->setWordWrap(true);
    status_->setEnabled(false);
    v->addWidget(status_);
    status_->setText(tr("Uses Claude Code installed on this computer, with your own Claude account. Nothing is sent until you write."));
}

void AgentChat::focusInput() { input_->setFocus(); }

AgentChat::~AgentChat() {
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(2000);
    }
}

bool AgentChat::start() {
    if (proc_ && proc_->state() != QProcess::NotRunning) return true;
    const QString claude = findClaude();
    if (claude.isEmpty()) {
        append("<p style='color:#b03a2e'>" +
               tr("Claude Code was not found. Install it from <a href='https://code.claude.com/docs/en/setup'>code.claude.com</a>, "
                  "open a terminal, run <code>claude</code> once to sign in with your Claude account, and try again.") +
               "</p>");
        return false;
    }
    QJsonObject server{{"command", cliPath()}, {"args", QJsonArray{"mcp", "--lang", language()}}};
    const QJsonObject config{{"mcpServers", QJsonObject{{"openrevers", server}}}};
    QStringList args = {"-p",
                        "--input-format", "stream-json",
                        "--output-format", "stream-json",
                        "--verbose",
                        "--include-partial-messages",
                        "--mcp-config", QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)),
                        "--strict-mcp-config",
                        // Only OpenRevers: no shell, no file editing.
                        "--tools", "",
                        "--allowedTools", "mcp__openrevers",
                        "--append-system-prompt", QString::fromUtf8(kSystem)};
    if (!sessionId_.isEmpty()) args << "--resume" << sessionId_;
    if (proc_) proc_->deleteLater();
    proc_ = new QProcess(this);
    proc_->setWorkingDirectory(QDir::homePath());
    connect(proc_, &QProcess::readyReadStandardOutput, this, &AgentChat::readOutput);
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(proc_->readAllStandardError()).trimmed();
        if (busy_) {
            append("<p style='color:#b03a2e'>" + tr("The agent stopped (code %1).").arg(code) + (err.isEmpty() ? QString() : "<br><small>" + err.right(600).toHtmlEscaped() + "</small>") + "</p>");
            setBusy(false);
        }
    });
    connect(proc_, &QProcess::errorOccurred, this, [this, claude](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            append("<p style='color:#b03a2e'>" + tr("Could not start %1.").arg(claude.toHtmlEscaped()) + "</p>");
            setBusy(false);
        }
    });
    status_->setText(tr("Starting Claude Code…"));
    proc_->start(claude, args);
    return true;
}

void AgentChat::send() {
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty() || busy_) return;
    QString message = text;
    if (withSelection_->isChecked() && selection_) {
        const QString sel = selection_();
        if (!sel.isEmpty()) message += "\n\n[" + tr("Picked in OpenRevers now: %1 (view_selection has the details)").arg(sel) + "]";
    }
    append("<p><b>" + tr("You") + ":</b> " + text.toHtmlEscaped().replace("\n", "<br>") + "</p>");
    input_->clear();
    if (!start()) return;
    const QJsonObject msg{{"type", "user"},
                          {"message", QJsonObject{{"role", "user"}, {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", message}}}}}}};
    proc_->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n");
    inText_ = false;
    setBusy(true);
}

void AgentChat::stop() {
    if (!proc_) return;
    // Ending the process ends the turn; the next message resumes the same
    // conversation (--resume), so nothing said so far is lost.
    proc_->disconnect(this);
    proc_->kill();
    proc_->waitForFinished(2000);
    proc_->deleteLater();
    proc_ = nullptr;
    emit stopRequested();
    append("<p style='color:#8796a3'>" + tr("Stopped.") + "</p>");
    setBusy(false);
}

void AgentChat::newConversation() {
    if (proc_) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(2000);
        proc_->deleteLater();
        proc_ = nullptr;
    }
    sessionId_.clear();
    view_->clear();
    setBusy(false);
    status_->setText(tr("New conversation."));
}

void AgentChat::readOutput() {
    buffer_ += proc_->readAllStandardOutput();
    qsizetype nl;
    while ((nl = buffer_.indexOf('\n')) >= 0) {
        const QByteArray line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);
        if (!line.isEmpty()) handle(line);
    }
}

void AgentChat::handle(const QByteArray& line) {
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    const QString type = o.value("type").toString();
    if (type == "system" && o.value("subtype").toString() == "init") {
        sessionId_ = o.value("session_id").toString(sessionId_);
        const QString model = o.value("model").toString();
        status_->setText(model.isEmpty() ? tr("Connected.") : tr("Connected · %1").arg(model));
    } else if (type == "system" && o.value("subtype").toString() == "api_retry") {
        status_->setText(tr("Retrying (%1)…").arg(o.value("attempt").toInt()));
    } else if (type == "stream_event") {
        const QJsonObject ev = o.value("event").toObject();
        const QString et = ev.value("type").toString();
        if (et == "content_block_start") {
            const QJsonObject block = ev.value("content_block").toObject();
            if (block.value("type").toString() == "text") {
                if (!inText_) append("<p><b>" + tr("Agent") + ":</b>&nbsp;</p>");
                inText_ = true;
            } else if (block.value("type").toString() == "tool_use") {
                QString name = block.value("name").toString();
                name.remove("mcp__openrevers__");
                append("<p style='color:#8796a3'>⚙ " + name.toHtmlEscaped() + "</p>");
                inText_ = false;
            }
        } else if (et == "content_block_delta") {
            const QJsonObject d = ev.value("delta").toObject();
            if (d.value("type").toString() == "text_delta") appendText(d.value("text").toString());
        }
    } else if (type == "result") {
        sessionId_ = o.value("session_id").toString(sessionId_);
        const bool error = o.value("is_error").toBool() || o.value("subtype").toString() != "success";
        if (error) {
            const QString r = o.value("result").toString();
            QString hint;
            if (r.contains("login", Qt::CaseInsensitive) || r.contains("auth", Qt::CaseInsensitive))
                hint = "<br>" + tr("Open a terminal, run <code>claude</code> and sign in with your Claude account, then try again.");
            append("<p style='color:#b03a2e'>" + (r.isEmpty() ? tr("The agent could not finish.") : r.toHtmlEscaped()) + hint + "</p>");
        }
        inText_ = false;
        setBusy(false);
    }
}

void AgentChat::append(const QString& html) {
    // A paragraph of its own; streamed text then continues in it.
    QString h = html;
    if (h.endsWith("</p>")) h.chop(4);
    if (h.startsWith("<p")) h = "<span" + h.mid(2) + "</span>";
    view_->append(h);
    view_->moveCursor(QTextCursor::End);
    view_->ensureCursorVisible();
}

void AgentChat::appendText(const QString& text) {
    view_->moveCursor(QTextCursor::End);
    view_->insertPlainText(text);
    view_->ensureCursorVisible();
}

void AgentChat::setBusy(bool busy) {
    busy_ = busy;
    sendButton_->setEnabled(!busy);
    stopButton_->setEnabled(busy);
    if (busy) status_->setText(tr("The agent is working…"));
    else if (proc_) status_->setText(tr("Ready."));
}

}  // namespace orv
