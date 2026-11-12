// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the main window: menus and toolbar built from the commands,
// the model tree, the properties, the history, the log and the 3D view.
#pragma once

#include "app/Viewport.h"

#include "app/AgentServer.h"
#include "app/CommandRunner.h"
#include "commands/Command.h"
#include "doc/Document.h"

#include <QMainWindow>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTreeWidget;
class QTableWidget;
class QProgressBar;
class QTreeWidgetItem;

namespace orv {

class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& lang, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Import or open what was given on the command line or dropped on the window.
    void openPaths(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void buildActions();
    void buildDocks();
    void refresh();
    void refreshTree();
    void refreshProperties();
    void refreshShapes();
    void refreshHistory();
    void refreshTitle();
    void refreshAgentStatus();
    QString selectedRef() const;

    /// Run a command as the user; asks for its inputs when it has more than a body.
    void runFromUi(const QString& name);
    void run(const QString& name, const json& params, std::function<void(const json&)> then = {});
    void log(const QString& who, const QString& text, bool error = false);

    void importMesh();
    void exportMesh();
    void exportTsm();
    void openProject();
    bool saveProject(bool saveAs);
    bool confirmDiscard();
    void showSettings();
    void showGuide();
    void showAbout();
    void copyAgentConfig(bool claudeCode);
    QString cliPath() const;

    QString lang_;
    Document doc_;
    CommandRegistry registry_;
    Viewport* viewport_{nullptr};
    CommandRunner* runner_{nullptr};
    AgentServer* agent_{nullptr};

    QTreeWidget* tree_{nullptr};
    QTableWidget* props_{nullptr};
    QTreeWidget* shapes_{nullptr};
    // Picking: the last point clicked on a surface, and the shapes picked, in order.
    Pick lastPick_;
    QStringList pickedShapes_;
    bool updatingShapes_{false};
    void onPicked(const Pick& p, bool add);
    void showContextMenu(const Pick& p, const QPoint& globalPos);
    void selectBody(std::uint64_t id);
    void syncShapeSelection();
    // A part: the item picked in its history ("p:<name>", "s<id>", "o<id>"; empty:
    // the part itself), and the faces and edges of its solid picked in the view.
    QString partItem_;
    std::vector<int> pickedFaces_, pickedEdges_;
    const void* pickedOn_{nullptr};  // the part's view the picks index into
    bool updatingProps_{false};
    void refreshPartHighlight();
    void showTreeMenu(const QPoint& pos);
    void showPartProperties(const Body& b);
    void onPropertyEdited(int row);
    void editSketch(std::uint64_t body, std::uint64_t sketch);
    void newSketchOnFace();
    void filletPickedEdges();
    void compareWithScan();
    QListWidget* history_{nullptr};
    QPlainTextEdit* log_{nullptr};
    QLabel* status_{nullptr};
    QLabel* agentStatus_{nullptr};
    QProgressBar* busy_{nullptr};
    std::uint64_t selected_{0};
    bool refreshQueued_{false};
    bool updatingTree_{false};
};

}  // namespace orv
