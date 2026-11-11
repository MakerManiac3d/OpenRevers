// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/MainWindow.h"

#include "app/AppCommands.h"
#include "app/CommandDialog.h"
#include "app/SettingsDialog.h"
#include "app/Viewport.h"
#include "commands/Guide.h"
#include "core/util/Version.h"

#include <QDir>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace orv {

MainWindow::MainWindow(const QString& lang, QWidget* parent) : QMainWindow(parent), lang_(lang) {
    setObjectName("MainWindow");
    setAcceptDrops(true);
    registerCoreCommands(registry_);
    viewport_ = new Viewport(this);
    setCentralWidget(viewport_);
    registerAppCommands(registry_, *viewport_);
    runner_ = new CommandRunner(registry_, doc_, this);
    agent_ = new AgentServer(*runner_, this);

    // The document may change on a worker thread: refresh on ours, once per burst.
    doc_.setOnChanged([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            if (refreshQueued_) return;
            refreshQueued_ = true;
            QMetaObject::invokeMethod(this, [this]() { refreshQueued_ = false; refresh(); }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    });

    buildActions();
    buildDocks();

    status_ = new QLabel();
    agentStatus_ = new QLabel();
    busy_ = new QProgressBar();
    busy_->setRange(0, 0);
    busy_->setMaximumWidth(140);
    busy_->setVisible(false);
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(busy_);
    statusBar()->addPermanentWidget(agentStatus_);

    connect(runner_, &CommandRunner::busyChanged, busy_, &QWidget::setVisible);
    connect(runner_, &CommandRunner::started, this, [this](const QString& title, const QString& caller) {
        status_->setText(caller == "agent" ? tr("Agent: %1…").arg(title) : tr("%1…").arg(title));
    });
    connect(runner_, &CommandRunner::finished, this, [this](const QString& title, const QString& caller, bool ok, const QString& summary) {
        const QString who = caller == "agent" ? tr("Agent") : tr("You");
        log(who, summary.isEmpty() ? title : title + ": " + summary, !ok);
        status_->setText(ok ? summary : tr("Failed: %1").arg(summary));
    });
    connect(agent_, &AgentServer::statusChanged, this, &MainWindow::refreshAgentStatus);
    connect(viewport_, &Viewport::bodyClicked, this, [this](std::uint64_t) {});

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    if (!settings.contains("window/geometry")) resize(1440, 900);

    agent_->start();
    refresh();
    log(tr("OpenRevers"), tr("Version %1. Everything runs on this computer.").arg(kVersionString));
}

MainWindow::~MainWindow() { doc_.setOnChanged({}); }

void MainWindow::buildActions() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    auto* meshMenu = menuBar()->addMenu(tr("&Mesh"));
    auto* pointsMenu = menuBar()->addMenu(tr("&Points"));
    auto* shapesMenu = menuBar()->addMenu(tr("S&hapes"));
    auto* alignMenu = menuBar()->addMenu(tr("&Align"));
    auto* bodyMenu = menuBar()->addMenu(tr("&Body"));
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* agentMenu = menuBar()->addMenu(tr("&AI agent"));
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    auto* tb = addToolBar(tr("Main"));
    tb->setObjectName("MainToolbar");
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(22, 22));
    QStyle* st = style();
    auto add = [&](QMenu* menu, const QString& text, auto slot, QKeySequence key = {}, QStyle::StandardPixmap icon = QStyle::SP_CustomBase, bool onToolbar = false) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        if (icon != QStyle::SP_CustomBase) a->setIcon(st->standardIcon(icon));
        connect(a, &QAction::triggered, this, slot);
        menu->addAction(a);
        if (onToolbar) tb->addAction(a);
        return a;
    };
    auto cmdText = [this](const char* name) {
        const Command* c = registry_.find(name);
        return QString::fromStdString(c->title.in(lang_.toStdString()));
    };

    add(fileMenu, tr("&New project"), [this]() { if (confirmDiscard()) run("project.new", {{"discard", true}}); }, QKeySequence::New, QStyle::SP_FileIcon);
    add(fileMenu, tr("&Open project…"), &MainWindow::openProject, QKeySequence::Open, QStyle::SP_DialogOpenButton);
    add(fileMenu, tr("&Save project"), [this]() { saveProject(false); }, QKeySequence::Save, QStyle::SP_DialogSaveButton, true);
    add(fileMenu, tr("Save project &as…"), [this]() { saveProject(true); }, QKeySequence::SaveAs);
    fileMenu->addSeparator();
    add(fileMenu, tr("&Import mesh…"), &MainWindow::importMesh, QKeySequence(Qt::CTRL | Qt::Key_I), QStyle::SP_ArrowDown, true);
    add(fileMenu, tr("&Export mesh…"), &MainWindow::exportMesh, QKeySequence(Qt::CTRL | Qt::Key_E), QStyle::SP_ArrowUp, true);
    add(fileMenu, tr("Export &T-Spline for Fusion (.tsm)…"), &MainWindow::exportTsm);
    fileMenu->addSeparator();
    add(fileMenu, tr("&Quit"), &QWidget::close, QKeySequence::Quit);
    tb->addSeparator();

    auto* undo = add(editMenu, tr("&Undo"), [this]() { run("history.undo", json::object()); }, QKeySequence::Undo, QStyle::SP_ArrowBack, true);
    auto* redo = add(editMenu, tr("&Redo"), [this]() { run("history.redo", json::object()); }, QKeySequence::Redo, QStyle::SP_ArrowForward, true);
    undo->setObjectName("undo");
    redo->setObjectName("redo");
    tb->addSeparator();

    for (const char* name : {"mesh.analyze", "mesh.repair", "mesh.fill_holes", "mesh.remove_pieces", "mesh.flip_normals", "mesh.smooth", "mesh.decimate", "mesh.remesh", "mesh.remesh_quads"}) {
        const bool onTb = std::string(name) != "mesh.flip_normals" && std::string(name) != "mesh.remove_pieces";
        auto* a = add(meshMenu, cmdText(name), [this, name]() { runFromUi(name); }, {}, QStyle::SP_CustomBase, onTb);
        a->setToolTip(QString::fromStdString(registry_.find(name)->description.in(lang_.toStdString())));
    }
    for (const char* name : {"features.segment", "features.fit", "features.refit", "features.measure", "features.rename", "features.delete"}) {
        auto* a = add(shapesMenu, cmdText(name), [this, name]() { runFromUi(name); });
        a->setToolTip(QString::fromStdString(registry_.find(name)->description.in(lang_.toStdString())));
    }
    for (const char* name : {"align.principal", "align.datums", "align.best_fit", "align.move"}) {
        auto* a = add(alignMenu, cmdText(name), [this, name]() { runFromUi(name); });
        a->setToolTip(QString::fromStdString(registry_.find(name)->description.in(lang_.toStdString())));
    }
    for (const char* name : {"points.thin", "points.estimate_normals", "points.to_mesh"}) {
        auto* a = add(pointsMenu, cmdText(name), [this, name]() { runFromUi(name); });
        a->setToolTip(QString::fromStdString(registry_.find(name)->description.in(lang_.toStdString())));
    }
    for (const char* name : {"body.rename", "body.duplicate", "body.delete"}) add(bodyMenu, cmdText(name), [this, name]() { runFromUi(name); });

    for (auto [text, view] : std::initializer_list<std::pair<QString, const char*>>{{tr("Isometric"), "iso"}, {tr("Front"), "front"}, {tr("Back"), "back"},
                                                                                  {tr("Left"), "left"}, {tr("Right"), "right"}, {tr("Top"), "top"}, {tr("Bottom"), "bottom"}}) {
        const QString v = view;
        add(viewMenu, text, [this, v]() { viewport_->setView(Viewport::viewFromName(v, nullptr)); });
    }
    add(viewMenu, tr("&Fit all"), [this]() { viewport_->fitAll(); }, QKeySequence(Qt::Key_F));
    viewMenu->addSeparator();
    auto toggle = [&](const QString& text, bool on, auto setter) {
        auto* a = new QAction(text, this);
        a->setCheckable(true);
        a->setChecked(on);
        connect(a, &QAction::toggled, this, setter);
        viewMenu->addAction(a);
        return a;
    };
    toggle(tr("Mesh edges"), false, [this](bool on) { viewport_->setWireframe(on); });
    toggle(tr("Open edges"), true, [this](bool on) { viewport_->setOpenEdges(on); });
    toggle(tr("Flat shading"), false, [this](bool on) { viewport_->setFlatShading(on); });
    toggle(tr("Shapes by colour"), true, [this](bool on) { viewport_->setShowShapes(on); });
    viewMenu->addSeparator();
    viewMenu->setObjectName("viewMenu");

    add(agentMenu, tr("Copy configuration for Claude &Desktop"), [this]() { copyAgentConfig(false); });
    add(agentMenu, tr("Copy command for Claude &Code"), [this]() { copyAgentConfig(true); });
    agentMenu->addSeparator();
    add(agentMenu, tr("&Settings…"), &MainWindow::showSettings);

    add(helpMenu, tr("&Guide"), &MainWindow::showGuide, QKeySequence::HelpContents);
    add(helpMenu, tr("Source code (GitHub)"), []() { QDesktopServices::openUrl(QUrl(kHomepage)); });
    add(helpMenu, tr("&About OpenRevers"), &MainWindow::showAbout);
    add(fileMenu, tr("Se&ttings…"), &MainWindow::showSettings);
}

void MainWindow::buildDocks() {
    auto dock = [this](const QString& title, const char* name, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        addDockWidget(area, d);
        if (auto* m = menuBar()->findChild<QMenu*>("viewMenu")) m->addAction(d->toggleViewAction());
        return d;
    };
    tree_ = new QTreeWidget();
    tree_->setHeaderLabels({tr("Body"), tr("Triangles")});
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->setRootIsDecorated(false);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        auto items = tree_->selectedItems();
        selected_ = items.isEmpty() ? 0 : items.first()->data(0, Qt::UserRole).toULongLong();
        viewport_->setSelected(selected_);
        refreshProperties();
        refreshShapes();
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
        if (updatingTree_ || col != 0) return;
        const QString ref = "b" + QString::number(item->data(0, Qt::UserRole).toULongLong());
        run("body.set_visible", {{"body", ref.toStdString()}, {"visible", item->checkState(0) == Qt::Checked}});
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { runFromUi("body.rename"); });
    dock(tr("Model"), "ModelDock", tree_, Qt::LeftDockWidgetArea);

    props_ = new QTableWidget(0, 2);
    props_->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    props_->horizontalHeader()->setStretchLastSection(true);
    props_->verticalHeader()->setVisible(false);
    props_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    props_->setWordWrap(false);
    dock(tr("Properties"), "PropertiesDock", props_, Qt::LeftDockWidgetArea);
    resizeDocks({findChild<QDockWidget*>("ModelDock")}, {340}, Qt::Horizontal);

    shapes_ = new QTreeWidget();
    shapes_->setHeaderLabels({tr("Shape"), tr("Size"), tr("σ (mm)")});
    shapes_->setRootIsDecorated(false);
    shapes_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    connect(shapes_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { runFromUi("features.rename"); });
    dock(tr("Shapes"), "ShapesDock", shapes_, Qt::RightDockWidgetArea);

    history_ = new QListWidget();
    dock(tr("History"), "HistoryDock", history_, Qt::RightDockWidgetArea);

    log_ = new QPlainTextEdit();
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    dock(tr("Log"), "LogDock", log_, Qt::BottomDockWidgetArea);
}

QString MainWindow::selectedRef() const { return selected_ ? "b" + QString::number(selected_) : QString(); }

void MainWindow::refresh() {
    const auto bodies = doc_.bodies();
    bool still = false;
    for (const auto& b : bodies) still = still || b.id == selected_;
    if (!still) selected_ = bodies.empty() ? 0 : bodies.back().id;
    viewport_->setBodies(bodies);
    viewport_->setSelected(selected_);
    refreshTree();
    refreshProperties();
    refreshShapes();
    refreshHistory();
    refreshTitle();
    if (auto* u = findChild<QAction*>("undo")) u->setEnabled(doc_.canUndo());
    if (auto* r = findChild<QAction*>("redo")) r->setEnabled(doc_.canRedo());
}

void MainWindow::refreshTree() {
    updatingTree_ = true;
    tree_->clear();
    for (const auto& b : doc_.bodies()) {
        const QString count = b.mesh->isPointCloud() ? tr("%1 points").arg(QLocale().toString(static_cast<qlonglong>(b.mesh->vertexCount())))
                                                     : QLocale().toString(static_cast<qlonglong>(b.mesh->faceCount()));
        auto* item = new QTreeWidgetItem(tree_, {QString::fromStdString(b.name), count});
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(b.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, b.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, QString::fromStdString(b.ref() + (b.source.empty() ? "" : "\n" + b.source)));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        if (b.id == selected_) item->setSelected(true);
    }
    updatingTree_ = false;
}

void MainWindow::refreshProperties() {
    props_->setRowCount(0);
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b) return;
    const Box3 bb = b->mesh->bounds();
    QLocale loc;
    auto row = [&](const QString& k, const QString& v) {
        const int r = props_->rowCount();
        props_->insertRow(r);
        props_->setItem(r, 0, new QTableWidgetItem(k));
        props_->setItem(r, 1, new QTableWidgetItem(v));
    };
    auto mm = [&](float v) { return loc.toString(v, 'f', 3); };
    row(tr("Name"), QString::fromStdString(b->name));
    row(tr("Reference"), QString::fromStdString(b->ref()));
    if (b->mesh->isPointCloud()) {
        row(tr("Points"), loc.toString(static_cast<qlonglong>(b->mesh->vertexCount())));
        row(tr("Normals"), b->mesh->hasNormals() ? tr("Yes") : tr("No"));
    } else {
        row(tr("Triangles"), loc.toString(static_cast<qlonglong>(b->mesh->faceCount())));
        row(tr("Vertices"), loc.toString(static_cast<qlonglong>(b->mesh->vertexCount())));
    }
    if (b->mesh->hasQuads()) row(tr("Quads"), loc.toString(static_cast<qlonglong>(b->mesh->quads.size())));
    row(tr("Size X × Y × Z (mm)"), mm(bb.size().x()) + " × " + mm(bb.size().y()) + " × " + mm(bb.size().z()));
    row(tr("Minimum (mm)"), mm(bb.min.x()) + ", " + mm(bb.min.y()) + ", " + mm(bb.min.z()));
    row(tr("Maximum (mm)"), mm(bb.max.x()) + ", " + mm(bb.max.y()) + ", " + mm(bb.max.z()));
    if (!b->mesh->isPointCloud()) row(tr("Area (mm²)"), loc.toString(b->mesh->area(), 'f', 2));
    if (!b->source.empty()) row(tr("Source"), QString::fromStdString(b->source));
    props_->resizeColumnsToContents();
    props_->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::refreshShapes() {
    shapes_->clear();
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b || !b->features) return;
    QLocale loc;
    auto n = [&](double v, int d = 3) { return loc.toString(v, 'f', d); };
    for (const auto& f : b->features->list) {
        const Primitive& s = f.shape;
        QString size;
        switch (s.type) {
            case PrimitiveType::Plane: size = n(f.area, 1) + " mm²"; break;
            case PrimitiveType::Sphere:
            case PrimitiveType::Cylinder: size = "Ø " + n(2 * s.radius); break;
            case PrimitiveType::Cone: size = n(2 * s.angle * 180 / 3.14159265358979323846, 2) + "°"; break;
            case PrimitiveType::Torus: size = "R " + n(s.radius) + " / r " + n(s.radius2); break;
        }
        auto* item = new QTreeWidgetItem(shapes_, {QString::fromStdString(f.name), size, n(s.rms, 4)});
        item->setToolTip(0, QString::fromStdString(f.ref() + " · " + primitiveName(s.type)));
        item->setData(0, Qt::UserRole, QString::fromStdString(f.ref()));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    }
    shapes_->resizeColumnToContents(1);
    shapes_->resizeColumnToContents(2);
}

void MainWindow::refreshHistory() {
    history_->clear();
    for (const auto& h : doc_.undoList()) history_->addItem(QString::fromStdString(h.label));
    for (const auto& h : doc_.redoList()) {
        auto* it = new QListWidgetItem(QString::fromStdString(h.label));
        it->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        history_->addItem(it);
    }
    history_->scrollToBottom();
}

void MainWindow::refreshTitle() {
    const QString path = QString::fromStdString(doc_.path());
    const QString name = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
    setWindowTitle(QString("%1%2 — OpenRevers").arg(name, doc_.modified() ? " *" : ""));
}

void MainWindow::refreshAgentStatus() {
    if (agent_->listening()) {
        agentStatus_->setText(agent_->clients() > 0 ? tr("AI agent connected") : tr("AI agent: ready on 127.0.0.1:%1").arg(agent_->port()));
        agentStatus_->setToolTip(tr("What the agent reads goes to its AI model. Nothing else leaves this computer."));
    } else {
        agentStatus_->setText(agent_->error().isEmpty() ? tr("AI agent: off") : tr("AI agent: port %1 busy").arg(agent_->port()));
        agentStatus_->setToolTip(agent_->error());
    }
}

void MainWindow::log(const QString& who, const QString& text, bool error) {
    const QString line = QString("%1  %2: %3%4").arg(QTime::currentTime().toString("HH:mm:ss"), who, error ? tr("ERROR ") : QString(), text);
    log_->appendPlainText(line);
}

void MainWindow::run(const QString& name, const json& params, std::function<void(const json&)> then) {
    runner_->submit(name, params, "user", PathPolicy::unrestricted(), lang_,
                    [this, then](bool ok, const json& result, const QString&, const QString& message) {
                        if (!ok) {
                            QMessageBox::warning(this, tr("OpenRevers"), message);
                            return;
                        }
                        if (then) then(result);
                    });
}

void MainWindow::runFromUi(const QString& name) {
    const Command* c = registry_.find(name.toStdString());
    if (!c) return;
    const auto bodies = doc_.bodies();
    const json& props = c->schema.value("properties", json::object());
    const bool needsBody = props.contains("body");
    if (needsBody && bodies.empty()) {
        QMessageBox::information(this, tr("OpenRevers"), tr("Import a mesh first (File → Import mesh)."));
        return;
    }
    json params;
    if (props.size() == 1 && needsBody && !selectedRef().isEmpty()) {
        params = {{"body", selectedRef().toStdString()}};
    } else {
        QString feature;
        if (shapes_ && !shapes_->selectedItems().isEmpty()) feature = shapes_->selectedItems().first()->data(0, Qt::UserRole).toString();
        CommandDialog dlg(*c, bodies, selectedRef(), lang_, this, feature);
        if (dlg.exec() != QDialog::Accepted) return;
        params = dlg.values();
    }
    run(name, params, [this, name](const json& r) {
        if (name == "mesh.analyze") {
            QMessageBox box(this);
            box.setWindowTitle(tr("Mesh analysis"));
            box.setText(QString::fromStdString(r.value("summary", "")));
            box.setDetailedText(QString::fromStdString(r.dump(2)));
            box.exec();
        }
    });
}

void MainWindow::importMesh() {
    QSettings s;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import mesh"), s.value("dirs/import").toString(), tr("Meshes and points (*.stl *.obj *.ply *.3mf *.e57 *.xyz *.pts);;All files (*)"));
    if (files.isEmpty()) return;
    s.setValue("dirs/import", QFileInfo(files.first()).absolutePath());
    openPaths(files);
}

void MainWindow::openPaths(const QStringList& paths) {
    for (const QString& p : paths) {
        if (p.endsWith(".orvp", Qt::CaseInsensitive)) {
            if (!confirmDiscard()) return;
            run("project.open", {{"path", p.toStdString()}, {"discard", true}}, [this](const json&) { viewport_->fitAll(); });
        } else {
            run("mesh.import", {{"path", p.toStdString()}});
        }
    }
}

void MainWindow::exportMesh() {
    if (selectedRef().isEmpty()) {
        QMessageBox::information(this, tr("OpenRevers"), tr("Select a body to export."));
        return;
    }
    QSettings s;
    const QString f = QFileDialog::getSaveFileName(this, tr("Export mesh"), s.value("dirs/export").toString(), tr("STL (*.stl);;OBJ (*.obj);;PLY (*.ply);;3MF (*.3mf);;XYZ points (*.xyz)"));
    if (f.isEmpty()) return;
    s.setValue("dirs/export", QFileInfo(f).absolutePath());
    // The file dialog has already asked about replacing an existing file.
    run("mesh.export", {{"body", selectedRef().toStdString()}, {"path", f.toStdString()}, {"overwrite", true}});
}

void MainWindow::exportTsm() {
    if (selectedRef().isEmpty()) {
        QMessageBox::information(this, tr("OpenRevers"), tr("Select a body to export."));
        return;
    }
    QSettings s;
    const QString f = QFileDialog::getSaveFileName(this, tr("Export T-Spline"), s.value("dirs/export").toString(), tr("T-Spline (*.tsm)"));
    if (f.isEmpty()) return;
    s.setValue("dirs/export", QFileInfo(f).absolutePath());
    run("mesh.export_tsm", {{"body", selectedRef().toStdString()}, {"path", f.toStdString()}, {"overwrite", true}});
}

void MainWindow::openProject() {
    if (!confirmDiscard()) return;
    QSettings s;
    const QString f = QFileDialog::getOpenFileName(this, tr("Open project"), s.value("dirs/project").toString(), tr("OpenRevers project (*.orvp)"));
    if (f.isEmpty()) return;
    s.setValue("dirs/project", QFileInfo(f).absolutePath());
    run("project.open", {{"path", f.toStdString()}, {"discard", true}}, [this](const json&) { viewport_->fitAll(); });
}

bool MainWindow::saveProject(bool saveAs) {
    QString path = QString::fromStdString(doc_.path());
    if (saveAs || path.isEmpty()) {
        QSettings s;
        path = QFileDialog::getSaveFileName(this, tr("Save project"), s.value("dirs/project").toString(), tr("OpenRevers project (*.orvp)"));
        if (path.isEmpty()) return false;
        s.setValue("dirs/project", QFileInfo(path).absolutePath());
    }
    run("project.save", {{"path", path.toStdString()}, {"overwrite", true}});
    return true;
}

bool MainWindow::confirmDiscard() {
    if (!doc_.modified()) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("The project has unsaved changes. Save them first?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        if (!saveProject(false)) return false;
        // The save runs in the queue before anything asked after it.
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    openPaths(paths);
}

QString MainWindow::cliPath() const {
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/openrevers-cli.exe");
#else
    return QCoreApplication::applicationDirPath() + "/openrevers-cli";
#endif
}

void MainWindow::copyAgentConfig(bool claudeCode) {
    QString text;
    if (claudeCode) {
        text = QString("claude mcp add --scope user openrevers -- \"%1\" mcp").arg(cliPath());
    } else {
        json j = {{"mcpServers", {{"openrevers", {{"command", cliPath().toStdString()}, {"args", {"mcp"}}}}}}};
        text = QString::fromStdString(j.dump(2));
    }
    QApplication::clipboard()->setText(text);
    QMessageBox::information(this, tr("Copied"),
                             claudeCode ? tr("Paste this command in a terminal:\n\n%1").arg(text)
                                        : tr("In Claude Desktop: Settings → Developer → Edit Config, paste the \"openrevers\" entry inside \"mcpServers\", then quit Claude Desktop completely and open it again.\n\n%1").arg(text));
}

void MainWindow::showSettings() {
    SettingsDialog dlg(lang_, this);
    if (dlg.exec() == QDialog::Accepted) {
        agent_->start();
        if (dlg.languageChanged()) QMessageBox::information(this, tr("Language"), tr("The new language is used the next time OpenRevers starts."));
    }
}

void MainWindow::showGuide() {
    auto* view = new QTextBrowser();
    view->setAttribute(Qt::WA_DeleteOnClose);
    view->setWindowTitle(tr("OpenRevers guide"));
    view->setMarkdown(QString::fromStdString(guideText(lang_.toStdString(), "all")));
    view->resize(720, 640);
    view->show();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About OpenRevers"),
                       tr("<h3>OpenRevers %1</h3><p>Reverse engineering of scanned meshes into parametric solids, entirely on this computer.</p>"
                          "<p>© 2026 MakerManiac3d. Free software under the GNU General Public License, version 3 or later.</p>"
                          "<p>Built on Qt, PMP Library, Eigen and nlohmann/json; see THIRD_PARTY.md.</p><p><a href=\"%2\">%2</a></p>")
                           .arg(kVersionString, kHomepage));
}

}  // namespace orv
