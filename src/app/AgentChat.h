// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — a chat with the AI agent inside the program. It runs the
// person's own Claude Code (the `claude` program, unmodified, signed in with
// their own account through Anthropic's sign-in) in print mode with JSON
// lines in and out, connected only to OpenRevers' MCP server. OpenRevers
// never sees or stores Claude credentials. Nothing runs until the person
// sends a message.
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTextBrowser;

namespace orv {

class AgentChat : public QWidget {
    Q_OBJECT
public:
    explicit AgentChat(QWidget* parent = nullptr);
    ~AgentChat() override;

    /// Words that say what is picked now, added to a message when the
    /// person ticks "Include the selection".
    void setSelectionSource(std::function<QString()> source) { selection_ = std::move(source); }
    /// The program to run (empty: `claude` from PATH or its usual places).
    static QString findClaude();
    /// Puts the cursor in the message box.
    void focusInput();

public slots:
    void send();
    void stop();
    void newConversation();

private:
    bool start();
    void readOutput();
    void handle(const QByteArray& line);
    void append(const QString& html);
    void appendText(const QString& text);
    void setBusy(bool busy);

    QTextBrowser* view_{nullptr};
    QPlainTextEdit* input_{nullptr};
    QPushButton* sendButton_{nullptr};
    QPushButton* stopButton_{nullptr};
    QCheckBox* withSelection_{nullptr};
    QLabel* status_{nullptr};
    QProcess* proc_{nullptr};
    QByteArray buffer_;
    QString sessionId_;
    bool inText_{false};  // streaming an answer's text
    bool busy_{false};
    std::function<QString()> selection_;
};

}  // namespace orv
