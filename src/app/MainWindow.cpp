// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/MainWindow.h"

#include "scan/ScanBackend.h"

#include "app/AppCommands.h"
#include "app/CommandDialog.h"
#include "app/SettingsDialog.h"
#include "app/SketchEditor.h"
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
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QInputDialog>
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
#include <QTimer>
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
    SelectionAccess access;
    access.selection = [this]() {
        json j = {{"body", lastPick_.hit() ? json("b" + std::to_string(lastPick_.body)) : json(nullptr)}};
        if (lastPick_.hit()) {
            j["point"] = {lastPick_.point.x(), lastPick_.point.y(), lastPick_.point.z()};
            j["normal"] = {lastPick_.normal.x(), lastPick_.normal.y(), lastPick_.normal.z()};
        }
        json shapes = json::array();
        if (auto b = doc_.findBody(selectedRef().toStdString()); b && b->features)
            for (const auto& r : pickedShapes_)
                if (const Feature* f = b->features->find(r.toStdString())) shapes.push_back({{"ref", f->ref()}, {"name", f->name}, {"type", primitiveName(f->shape.type)}});
        j["shapes"] = shapes;
        // A part: the item picked in its history, and the faces and edges of its solid.
        if (auto b = doc_.findBody(selectedRef().toStdString()); b && b->isPart()) {
            if (!partItem_.isEmpty()) {
                const std::string it = partItem_.toStdString();
                j["partItem"] = it.rfind("p:", 0) == 0 ? json{{"parameter", it.substr(2)}} : json{{"ref", it}};
            }
            auto info = [](const ShapeInfo& i) {
                json k{{"type", i.type}, {"size", i.size}};
                if (i.radius > 0) k["radius"] = i.radius;
                if (i.radius2 > 0) k["radius2"] = i.radius2;
                if (i.axis.norm() > 0) k["axis"] = {i.axis.x(), i.axis.y(), i.axis.z()};
                k["origin"] = {i.origin.x(), i.origin.y(), i.origin.z()};
                return k;
            };
            json faces = json::array(), edges = json::array();
            for (int f : pickedFaces_) {
                json k = info(b->solid.faceInfo(f));
                k["index"] = f;
                faces.push_back(k);
            }
            for (int e : pickedEdges_) {
                json k = info(b->solid.edgeInfo(e));
                k["index"] = e;
                if (b->view && static_cast<std::size_t>(e) < b->view->edgeMiddle.size()) {
                    const Vec3d m = b->view->edgeMiddle[static_cast<std::size_t>(e)];
                    k["middle"] = {m.x(), m.y(), m.z()};  // pass to part.fillet's points
                }
                edges.push_back(k);
            }
            j["faces"] = faces;
            j["edges"] = edges;
        }
        return j;
    };
    access.pick = [this, selection = access.selection](double x, double y, bool add) {
        const QPoint pos(static_cast<int>(x * viewport_->width()), static_cast<int>(y * viewport_->height()));
        onPicked(viewport_->pickAt(pos), add);
        return selection();
    };
    registerAppCommands(registry_, *viewport_, access);
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
    connect(viewport_, &Viewport::picked, this, &MainWindow::onPicked);
    connect(viewport_, &Viewport::contextRequested, this, &MainWindow::showContextMenu);

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    if (!settings.contains("window/geometry")) resize(1440, 900);

    scanTimer_ = new QTimer(this);
    connect(scanTimer_, &QTimer::timeout, this, &MainWindow::pollScan);
    scanTimer_->start(400);

    agent_->start();
    refresh();
    log(tr("OpenRevers"), tr("Version %1. Everything runs on this computer.").arg(kVersionString));
}

MainWindow::~MainWindow() { doc_.setOnChanged({}); }

void MainWindow::pollScan() {
    // Never wait for the scanner here: a command may be using it.
    auto& mgr = ScanManager::instance();
    auto lock = mgr.tryLock();
    if (!lock.owns_lock() || !mgr.started()) return;
    std::shared_ptr<TriMesh> cloud;
    try {
        const ScanState st = mgr.backend().status().state;
        if (st == ScanState::Previewing || st == ScanState::Scanning || st == ScanState::Paused || st == ScanState::Stopped)
            cloud = mgr.backend().cloud(300000);
    } catch (...) {
        cloud.reset();
    }
    const bool show = cloud && !cloud->vertices.empty();
    if (show || scanShown_) viewport_->setLiveCloud(show ? cloud : nullptr);
    scanShown_ = show;
}

void MainWindow::buildActions() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    auto* meshMenu = menuBar()->addMenu(tr("&Mesh"));
    auto* pointsMenu = menuBar()->addMenu(tr("&Points"));
    auto* scanMenu = menuBar()->addMenu(tr("S&can"));
    auto* shapesMenu = menuBar()->addMenu(tr("S&hapes"));
    auto* alignMenu = menuBar()->addMenu(tr("&Align"));
    auto* partMenu = menuBar()->addMenu(tr("&Part"));
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
    add(fileMenu, cmdText("part.import_step") + "…", [this]() { runFromUi("part.import_step"); });
    add(fileMenu, cmdText("part.export_step") + "…", [this]() { runFromUi("part.export_step"); });
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
    for (const char* name : {"scan.status", "scan.connect", "scan.preview", "scan.start", "scan.pause", "scan.stop", "scan.export_to_body",
                             "scan.disconnect", "scan.use_simulator"}) {
        auto* a = add(scanMenu, cmdText(name), [this, name]() { runFromUi(name); });
        a->setToolTip(QString::fromStdString(registry_.find(name)->description.in(lang_.toStdString())));
        if (std::string(name) == "scan.connect" || std::string(name) == "scan.export_to_body") scanMenu->addSeparator();
    }
    for (const char* name : {"part.new", "part.sketch_from_section", "part.sketch", "part.extrude", "part.revolve", "part.loft", "part.pattern", "part.fillet",
                             "part.surface_from_scan", "part.cut_by_surface", "part.thicken", "part.parameter", "part.edit", "part.move", "part.delete", "part.list",
                             "part.compare", "mesh.section"}) {
        auto* a = add(partMenu, cmdText(name), [this, name]() { runFromUi(name); });
        a->setToolTip(QString::fromStdString(registry_.find(name)->description.in(lang_.toStdString())));
        if (std::string(name) == "part.sketch") {
            add(partMenu, tr("Edit the picked sketch…"), [this]() {
                if (partItem_.startsWith('s')) editSketch(selected_, partItem_.mid(1).toULongLong());
                else QMessageBox::information(this, tr("OpenRevers"), tr("Pick a sketch in the Model panel first."));
            });
            add(partMenu, tr("New sketch on the picked face…"), &MainWindow::newSketchOnFace);
        }
        if (std::string(name) == "part.fillet") {
            add(partMenu, tr("Round the picked edges…"), &MainWindow::filletPickedEdges);
            partMenu->addSeparator();
        }
        if (std::string(name) == "part.thicken") partMenu->addSeparator();
        if (std::string(name) == "part.list") partMenu->addSeparator();
    }
    for (const char* name : {"body.set_opacity", "body.clear_deviation"}) add(bodyMenu, cmdText(name), [this, name]() { runFromUi(name); });
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
    toggle(tr("Sketches of parts"), true, [this](bool on) { viewport_->setShowSketches(on); });
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
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showTreeMenu);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (updatingTree_) return;
        auto items = tree_->selectedItems();
        const std::uint64_t was = selected_;
        selected_ = items.isEmpty() ? 0 : items.first()->data(0, Qt::UserRole).toULongLong();
        partItem_ = items.isEmpty() ? QString() : items.first()->data(0, Qt::UserRole + 1).toString();
        if (selected_ != was) {
            pickedFaces_.clear();
            pickedEdges_.clear();
        }
        viewport_->setSelected(selected_);
        refreshPartHighlight();
        refreshProperties();
        refreshShapes();
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
        if (updatingTree_ || col != 0 || item->parent()) return;  // a part's history rows have no visibility
        const QString ref = "b" + QString::number(item->data(0, Qt::UserRole).toULongLong());
        run("body.set_visible", {{"body", ref.toStdString()}, {"visible", item->checkState(0) == Qt::Checked}});
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const QString it = item ? item->data(0, Qt::UserRole + 1).toString() : QString();
        if (it.startsWith('s') && !it.startsWith("sf")) editSketch(selected_, it.mid(1).toULongLong());
        else if (it.isEmpty()) runFromUi("body.rename");
        else if (props_->rowCount() > 0) {  // a parameter or an operation: its values are in the properties, ready to edit
            props_->setFocus();
            for (int r = 0; r < props_->rowCount(); ++r)
                if (props_->item(r, 1) && (props_->item(r, 1)->flags() & Qt::ItemIsEditable)) {
                    props_->setCurrentCell(r, 1);
                    props_->editItem(props_->item(r, 1));
                    break;
                }
        }
    });
    dock(tr("Model"), "ModelDock", tree_, Qt::LeftDockWidgetArea);

    props_ = new QTableWidget(0, 2);
    props_->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    props_->horizontalHeader()->setStretchLastSection(true);
    props_->verticalHeader()->setVisible(false);
    props_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    props_->setWordWrap(false);
    connect(props_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (!updatingProps_ && it && it->column() == 1) onPropertyEdited(it->row());
    });
    dock(tr("Properties"), "PropertiesDock", props_, Qt::LeftDockWidgetArea);
    resizeDocks({findChild<QDockWidget*>("ModelDock")}, {340}, Qt::Horizontal);

    shapes_ = new QTreeWidget();
    shapes_->setHeaderLabels({tr("Shape"), tr("Size"), tr("σ (mm)")});
    shapes_->setRootIsDecorated(false);
    shapes_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(shapes_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (updatingShapes_) return;
        // Keep the order in which shapes were picked: the dialogs fill primary, secondary… from it.
        QStringList now;
        for (auto* it : shapes_->selectedItems()) now << it->data(0, Qt::UserRole).toString();
        QStringList ordered;
        for (const auto& r : pickedShapes_)
            if (now.contains(r)) ordered << r;
        for (const auto& r : now)
            if (!ordered.contains(r)) ordered << r;
        pickedShapes_ = ordered;
        syncShapeSelection();
    });
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
        const QString count = b.isPart() ? tr("part")
                              : b.mesh->isPointCloud() ? tr("%1 points").arg(QLocale().toString(static_cast<qlonglong>(b.mesh->vertexCount())))
                                                       : QLocale().toString(static_cast<qlonglong>(b.mesh->faceCount()));
        auto* item = new QTreeWidgetItem(tree_, {QString::fromStdString(b.name), count});
        if (b.isPart()) {
            // The history under the part: parameters, sketches, then the operations in order.
            auto child = [&](const QString& text, const QString& note, const std::string& tip, const QString& id) {
                auto* c = new QTreeWidgetItem(item, {text, note});
                c->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(b.id));
                c->setData(0, Qt::UserRole + 1, id);
                c->setToolTip(0, QString::fromStdString(tip));
                c->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
                if (b.id == selected_ && id == partItem_) c->setSelected(true);
                return c;
            };
            for (const auto& [name, value] : b.model->parameters)
                child(QString::fromStdString(name), QLocale().toString(value, 'g', 6), tr("Parameter").toStdString(), "p:" + QString::fromStdString(name));
            for (const auto& sk : b.model->sketches) child(QString::fromStdString(sk.name), tr("sketch"), sk.ref() + " · " + tr("double-click to edit").toStdString(), QString::fromStdString(sk.ref()));
            for (const auto& sf : b.model->surfaces) child(QString::fromStdString(sf.name), tr("surface"), sf.ref(), QString::fromStdString(sf.ref()));
            for (const auto& op : b.model->operations) {
                auto* c = child(QString::fromStdString(op.name), QString::fromUtf8(opModeName(op.mode)), op.ref() + " · " + opKindName(op.kind), QString::fromStdString(op.ref()));
                if (op.suppressed) c->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
                if (!b.buildError.empty() && b.buildError.find(op.ref()) != std::string::npos) c->setForeground(0, QColor(200, 40, 40));
            }
            item->setExpanded(true);
        }
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(b.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, b.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, QString::fromStdString(b.ref() + (b.source.empty() ? "" : "\n" + b.source)));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        if (b.id == selected_ && partItem_.isEmpty()) item->setSelected(true);
    }
    updatingTree_ = false;
    refreshPartHighlight();
}

void MainWindow::refreshProperties() {
    updatingProps_ = true;
    struct Done { bool& f; ~Done() { f = false; } } done{updatingProps_};
    props_->setRowCount(0);
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b) return;
    const Box3 bb = b->mesh->bounds();
    QLocale loc;
    auto row = [&](const QString& k, const QString& v) {
        const int r = props_->rowCount();
        props_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(key->flags() & ~Qt::ItemIsEditable);
        auto* val = new QTableWidgetItem(v);
        val->setFlags(val->flags() & ~Qt::ItemIsEditable);
        props_->setItem(r, 0, key);
        props_->setItem(r, 1, val);
    };
    auto mm = [&](float v) { return loc.toString(v, 'f', 3); };
    if (b->isPart() && !partItem_.isEmpty()) {
        showPartProperties(*b);
        return;
    }
    row(tr("Name"), QString::fromStdString(b->name));
    row(tr("Reference"), QString::fromStdString(b->ref()));
    if (b->isPart()) {
        row(tr("Kind"), tr("Parametric part"));
        row(tr("Operations"), loc.toString(static_cast<qlonglong>(b->model->operations.size())));
        if (!b->solid.empty()) {
            row(tr("Volume (mm³)"), loc.toString(b->solid.volume(), 'f', 3));
            row(tr("Solid faces"), loc.toString(b->solid.faceCount()));
        }
        if (!b->buildError.empty()) row(tr("Error"), QString::fromStdString(b->buildError));
    } else if (b->mesh->isPointCloud()) {
        row(tr("Points"), loc.toString(static_cast<qlonglong>(b->mesh->vertexCount())));
        row(tr("Normals"), b->mesh->hasNormals() ? tr("Yes") : tr("No"));
    } else {
        row(tr("Triangles"), loc.toString(static_cast<qlonglong>(b->mesh->faceCount())));
        row(tr("Vertices"), loc.toString(static_cast<qlonglong>(b->mesh->vertexCount())));
    }
    if (b->mesh->hasQuads()) row(tr("Quads"), loc.toString(static_cast<qlonglong>(b->mesh->quads.size())));
    if (bb.empty()) {
        props_->resizeColumnsToContents();
        return;
    }
    row(tr("Size X × Y × Z (mm)"), mm(bb.size().x()) + " × " + mm(bb.size().y()) + " × " + mm(bb.size().z()));
    row(tr("Minimum (mm)"), mm(bb.min.x()) + ", " + mm(bb.min.y()) + ", " + mm(bb.min.z()));
    row(tr("Maximum (mm)"), mm(bb.max.x()) + ", " + mm(bb.max.y()) + ", " + mm(bb.max.z()));
    if (!b->mesh->isPointCloud()) row(tr("Area (mm²)"), loc.toString(b->mesh->area(), 'f', 2));
    if (!b->source.empty()) row(tr("Source"), QString::fromStdString(b->source));
    props_->resizeColumnsToContents();
    props_->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::selectBody(std::uint64_t id) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* it = tree_->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toULongLong() == id) {
            if (selected_ != id) tree_->setCurrentItem(it);
            return;
        }
    }
}

void MainWindow::onPicked(const Pick& p, bool add) {
    lastPick_ = p;
    viewport_->setPickMarker(p);
    if (!p.hit()) {
        if (!add) {
            pickedShapes_.clear();
            syncShapeSelection();
        }
        status_->clear();
        return;
    }
    if (p.body != selected_) {
        pickedFaces_.clear();
        pickedEdges_.clear();
    }
    selectBody(p.body);
    QLocale loc;
    QString where = tr("Point %1, %2, %3 mm").arg(loc.toString(p.point.x(), 'f', 3), loc.toString(p.point.y(), 'f', 3), loc.toString(p.point.z(), 'f', 3));
    if (auto b = doc_.findBody("b" + std::to_string(p.body)); b && b->isPart()) {
        // A part: an edge near the click wins over the face under it; Ctrl adds or takes away.
        auto toggle = [&](std::vector<int>& list, int i) {
            auto it = std::find(list.begin(), list.end(), i);
            if (add && it != list.end()) list.erase(it);
            else if (it == list.end()) list.push_back(i);
        };
        if (!add) {
            pickedFaces_.clear();
            pickedEdges_.clear();
        }
        auto describe = [&](const ShapeInfo& i, bool face) {
            QString t = face ? tr("Face") : tr("Edge");
            static const std::map<std::string, const char*> names{{"plane", QT_TR_NOOP("plane")}, {"cylinder", QT_TR_NOOP("cylinder")}, {"cone", QT_TR_NOOP("cone")},
                                                                  {"sphere", QT_TR_NOOP("sphere")}, {"torus", QT_TR_NOOP("torus")}, {"bspline", QT_TR_NOOP("free-form")},
                                                                  {"line", QT_TR_NOOP("line")}, {"circle", QT_TR_NOOP("circle")}, {"ellipse", QT_TR_NOOP("ellipse")}};
            auto n = names.find(i.type);
            t += " · " + (n != names.end() ? tr(n->second) : QString::fromStdString(i.type));
            if (i.radius > 0) t += " · Ø " + loc.toString(2 * i.radius, 'f', 3);
            t += face ? " · " + loc.toString(i.size, 'f', 2) + " mm²" : " · " + loc.toString(i.size, 'f', 3) + " mm";
            return t;
        };
        if (p.solidEdge >= 0) {
            toggle(pickedEdges_, p.solidEdge);
            where += " · " + describe(b->solid.edgeInfo(p.solidEdge), false);
        } else if (p.solidFace >= 0) {
            toggle(pickedFaces_, p.solidFace);
            where += " · " + describe(b->solid.faceInfo(p.solidFace), true);
        }
        const auto n = pickedFaces_.size() + pickedEdges_.size();
        if (n > 1) where += " · " + tr("%1 picked").arg(n);
        refreshPartHighlight();
        status_->setText(where);
        return;
    }
    // The shape whose region holds the clicked face.
    QString shape;
    if (auto b = doc_.findBody("b" + std::to_string(p.body)); b && b->features && p.face < b->features->faceRegion.size()) {
        const std::int32_t r = b->features->faceRegion[p.face];
        for (const auto& f : b->features->list)
            if (r >= 0 && f.region == r) {
                shape = QString::fromStdString(f.ref());
                where += " · " + QString::fromStdString(f.name) + tr(" (σ %1 mm)").arg(loc.toString(f.shape.rms, 'f', 4));
            }
    }
    if (!add) pickedShapes_.clear();
    if (!shape.isEmpty()) {
        if (add && pickedShapes_.contains(shape)) pickedShapes_.removeAll(shape);
        else if (!pickedShapes_.contains(shape)) pickedShapes_ << shape;
    }
    syncShapeSelection();
    if (pickedShapes_.size() > 1) where += " · " + tr("%1 shapes picked").arg(pickedShapes_.size());
    status_->setText(where);
}

void MainWindow::syncShapeSelection() {
    // Panel and view follow pickedShapes_.
    updatingShapes_ = true;
    shapes_->clearSelection();
    for (int i = 0; i < shapes_->topLevelItemCount(); ++i) {
        auto* it = shapes_->topLevelItem(i);
        if (pickedShapes_.contains(it->data(0, Qt::UserRole).toString())) {
            it->setSelected(true);
            shapes_->scrollToItem(it);
        }
    }
    updatingShapes_ = false;
    std::vector<std::int32_t> regions;
    if (auto b = doc_.findBody(selectedRef().toStdString()); b && b->features)
        for (const auto& r : pickedShapes_)
            if (const Feature* f = b->features->find(r.toStdString()); f && f->region >= 0) regions.push_back(f->region);
    viewport_->setHighlightedRegions(selected_, regions);
}

void MainWindow::showContextMenu(const Pick& p, const QPoint& globalPos) {
    if (p.hit() && (!pickedShapes_.size() || p.body != selected_)) onPicked(p, false);
    else if (p.hit()) { lastPick_ = p; viewport_->setPickMarker(p); }
    QMenu menu(this);
    auto item = [&](const char* name, bool enabled = true) {
        const Command* c = registry_.find(name);
        if (!c) return;
        QAction* a = menu.addAction(QString::fromStdString(c->title.in(lang_.toStdString())) + QStringLiteral("…"), this, [this, name]() { runFromUi(name); });
        a->setEnabled(enabled);
    };
    const bool onBody = p.hit();
    const bool hasShape = !pickedShapes_.isEmpty();
    if (auto b = doc_.findBody(selectedRef().toStdString()); onBody && b && b->isPart()) {
        // On a part: what can be done with the faces and edges picked.
        if (p.solidFace >= 0 && std::find(pickedFaces_.begin(), pickedFaces_.end(), p.solidFace) == pickedFaces_.end() && p.solidEdge < 0) {
            pickedFaces_ = {p.solidFace};
            pickedEdges_.clear();
            refreshPartHighlight();
        }
        QAction* sk = menu.addAction(tr("New sketch on this face…"), this, &MainWindow::newSketchOnFace);
        sk->setEnabled(pickedFaces_.size() == 1 && b->solid.faceInfo(pickedFaces_.front()).type == "plane");
        QAction* fl = menu.addAction(tr("Round the picked edges…"), this, &MainWindow::filletPickedEdges);
        fl->setEnabled(!pickedEdges_.empty());
        menu.addSeparator();
        menu.addAction(tr("Compare with a scan…"), this, &MainWindow::compareWithScan);
        menu.addAction(b->opacity < 1.f ? tr("Make opaque") : tr("See through"), this, [this, b]() {
            run("body.set_opacity", {{"body", b->ref()}, {"opacity", b->opacity < 1.f ? 1.0 : 0.4}});
        });
        menu.exec(globalPos);
        return;
    }
    if (auto b = doc_.findBody(selectedRef().toStdString()); onBody && b) {
        if (b->deviation) menu.addAction(tr("Remove deviation colours"), this, [this, b]() { run("body.clear_deviation", {{"body", b->ref()}}); });
        menu.addAction(b->opacity < 1.f ? tr("Make opaque") : tr("See through"), this, [this, b]() {
            run("body.set_opacity", {{"body", b->ref()}, {"opacity", b->opacity < 1.f ? 1.0 : 0.4}});
        });
        menu.addSeparator();
    }
    item("features.fit", onBody);
    item("features.refit", hasShape);
    item("features.measure", pickedShapes_.size() >= 2);
    menu.addSeparator();
    item("align.datums", hasShape);
    item("align.best_fit", onBody);
    menu.addSeparator();
    item("features.rename", hasShape);
    item("features.delete", hasShape);
    menu.addSeparator();
    item("features.segment", onBody);
    menu.exec(globalPos);
}

void MainWindow::refreshPartHighlight() {
    PartHighlight h;
    if (auto b = doc_.findBody(selectedRef().toStdString()); b && b->isPart()) {
        // A rebuilt solid numbers its faces and edges anew: the old picks mean nothing.
        if (pickedOn_ != b->view.get()) {
            pickedFaces_.clear();
            pickedEdges_.clear();
            pickedOn_ = b->view.get();
        }
        h.body = b->id;
        h.faces = pickedFaces_;
        h.edges = pickedEdges_;
        // The sketch picked, or the one the picked operation is made from.
        if (partItem_.startsWith("sf")) h.sketches.push_back(partItem_.mid(2).toULongLong());
        else if (partItem_.startsWith('s')) h.sketches.push_back(partItem_.mid(1).toULongLong());
        if (partItem_.startsWith('o'))
            if (const Operation* o = b->model->operation(partItem_.toStdString())) {
                if (o->kind == OpKind::Extrude || o->kind == OpKind::Revolve) h.sketches.push_back(o->sketch);
                if (o->kind == OpKind::Loft) h.sketches.insert(h.sketches.end(), o->sections.begin(), o->sections.end());
                if (o->kind == OpKind::SurfaceCut || o->kind == OpKind::Thicken) h.sketches.push_back(o->surface);
            }
    }
    viewport_->setPartHighlight(h);
}

namespace {
/// A value typed in the properties: a number (in the local or the C form) or a parameter's name.
json valueFromText(const QString& text, bool* ok) {
    const QString t = text.trimmed();
    bool good = false;
    double v = QLocale().toDouble(t, &good);
    if (!good) v = QLocale::c().toDouble(t, &good);
    *ok = good || (!t.isEmpty() && t.front().isLetter());
    return good ? json(v) : json(t.toStdString());
}
QString valueText(const Value& v) { return v.parameter.empty() ? QLocale().toString(v.number, 'g', 8) : QString::fromStdString(v.parameter); }
}  // namespace

void MainWindow::showPartProperties(const Body& b) {
    // Rows: a label, the value, and in the label's data the field the value edits.
    QLocale loc;
    auto row = [&](const QString& k, const QString& v, const QString& field = {}, const QString& tip = {}) {
        const int r = props_->rowCount();
        props_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(key->flags() & ~Qt::ItemIsEditable);
        key->setData(Qt::UserRole, field);
        auto* val = new QTableWidgetItem(v);
        if (field.isEmpty()) val->setFlags(val->flags() & ~Qt::ItemIsEditable);
        else {
            val->setToolTip(tip.isEmpty() ? tr("Double-click to change: a number, or a parameter's name") : tip);
            QFont f = val->font();
            f.setBold(true);
            val->setFont(f);
        }
        props_->setItem(r, 0, key);
        props_->setItem(r, 1, val);
        return r;
    };
    const std::string it = partItem_.toStdString();
    if (it.rfind("p:", 0) == 0) {
        const std::string name = it.substr(2);
        row(tr("Parameter"), QString::fromStdString(name));
        auto pv = b.model->parameters.find(name);
        row(tr("Value"), pv != b.model->parameters.end() ? loc.toString(pv->second, 'g', 10) : QString(), "value", tr("Double-click to change; the operations that use it are rebuilt"));
        QStringList users;
        for (const auto& o : b.model->operations)
            for (const Value* v : {&o.start, &o.end, &o.angle, &o.radius})
                if (v->parameter == name && !users.contains(QString::fromStdString(o.name))) users << QString::fromStdString(o.name);
        row(tr("Used by"), users.isEmpty() ? tr("nothing yet") : users.join(", "));
    } else if (const Sketch* sk = b.model->sketch(it)) {
        row(tr("Sketch"), QString::fromStdString(sk->name), "name", tr("Double-click to rename"));
        row(tr("Reference"), QString::fromStdString(sk->ref()));
        std::size_t segs = 0;
        for (const auto& l : sk->profile.loops) segs += l.segments.size();
        row(tr("Loops / segments"), QString("%1 / %2").arg(sk->profile.loops.size()).arg(segs));
        const auto& pl = sk->profile.plane;
        auto v3 = [&](const Vec3d& v) { return loc.toString(v.x(), 'f', 4) + "; " + loc.toString(v.y(), 'f', 4) + "; " + loc.toString(v.z(), 'f', 4); };
        row(tr("Plane origin"), v3(pl.origin), "origin", tr("Double-click to move the plane: x; y; z"));
        row(tr("Plane normal"), v3(pl.normal), "normal", tr("Double-click to turn the plane: x; y; z"));
        row(tr("Plane x direction"), v3(pl.xDir));
        row(tr("Edit"), tr("double-click the sketch in the Model panel"));
    } else if (const Operation* o = b.model->operation(it)) {
        row(tr("Operation"), QString::fromStdString(o->name), "name", tr("Double-click to rename"));
        row(tr("Reference"), QString::fromStdString(o->ref() + " · " + opKindName(o->kind)));
        const int modeRow = row(tr("Mode"), QString::fromUtf8(opModeName(o->mode)));
        auto* mode = new QComboBox();
        for (const char* m : {"new", "join", "cut", "intersect"}) mode->addItem(QString::fromUtf8(m));
        mode->setCurrentText(QString::fromUtf8(opModeName(o->mode)));
        connect(mode, &QComboBox::currentTextChanged, this, [this, ref = b.ref(), op = o->ref()](const QString& m) {
            run("part.edit", {{"body", ref}, {"operation", op}, {"mode", m.toStdString()}});
        });
        props_->setCellWidget(modeRow, 1, mode);
        const int supRow = row(tr("Suppressed"), QString());
        auto* sup = new QCheckBox(tr("left out of the part"));
        sup->setChecked(o->suppressed);
        connect(sup, &QCheckBox::toggled, this, [this, ref = b.ref(), op = o->ref()](bool on) {
            run("part.edit", {{"body", ref}, {"operation", op}, {"suppressed", on}});
        });
        props_->setCellWidget(supRow, 1, sup);
        switch (o->kind) {
            case OpKind::Extrude:
                if (const Sketch* s2 = b.model->sketch("s" + std::to_string(o->sketch))) row(tr("Sketch"), QString::fromStdString(s2->name));
                row(tr("From (mm)"), valueText(o->start), "start");
                row(tr("To (mm)"), valueText(o->end), "end");
                break;
            case OpKind::Revolve:
                if (const Sketch* s2 = b.model->sketch("s" + std::to_string(o->sketch))) row(tr("Sketch"), QString::fromStdString(s2->name));
                row(tr("Angle (°)"), valueText(o->angle), "angle");
                break;
            case OpKind::Fillet:
                row(tr("Edges"), loc.toString(static_cast<qlonglong>(o->edgePoints.size())));
                row(tr("Radius (mm)"), valueText(o->radius), "radius");
                break;
            case OpKind::Import: break;
            case OpKind::Loft: {
                QStringList names;
                for (auto id : o->sections)
                    if (const Sketch* s2 = b.model->sketch("s" + std::to_string(id))) names << QString::fromStdString(s2->name);
                row(tr("Sections"), names.join(" → "));
                const int r = row(tr("Ruled"), QString());
                auto* ruled = new QCheckBox(tr("flat between sections"));
                ruled->setChecked(o->ruled);
                connect(ruled, &QCheckBox::toggled, this, [this, ref = b.ref(), op = o->ref()](bool on) { run("part.edit", {{"body", ref}, {"operation", op}, {"ruled", on}}); });
                props_->setCellWidget(r, 1, ruled);
                break;
            }
            case OpKind::Pattern: {
                if (const Operation* src = b.model->operation("o" + std::to_string(o->source))) row(tr("Repeats"), QString::fromStdString(src->name));
                row(tr("Copies"), valueText(o->count), "count");
                row(tr("Angle (°)"), valueText(o->angle), "angle");
                auto v3 = [&](const Vec3d& v) { return loc.toString(v.x(), 'f', 3) + "; " + loc.toString(v.y(), 'f', 3) + "; " + loc.toString(v.z(), 'f', 3); };
                row(tr("Axis point"), v3(o->axisOrigin));
                row(tr("Axis direction"), v3(o->axisDirection));
                break;
            }
            case OpKind::SurfaceCut:
            case OpKind::Thicken: {
                if (const FreeSurface* f = b.model->surface("sf" + std::to_string(o->surface))) row(tr("Surface"), QString::fromStdString(f->name));
                if (o->kind == OpKind::Thicken) {
                    row(tr("Thickness (mm)"), valueText(o->thickness), "thickness");
                } else {
                    const int r = row(tr("Keeps"), QString());
                    auto* keep = new QComboBox();
                    keep->addItem(tr("below the surface"), "below");
                    keep->addItem(tr("above the surface"), "above");
                    keep->setCurrentIndex(o->keepBelow ? 0 : 1);
                    connect(keep, &QComboBox::currentIndexChanged, this, [this, keep, ref = b.ref(), op = o->ref()]() {
                        run("part.edit", {{"body", ref}, {"operation", op}, {"keep", keep->currentData().toString().toStdString()}});
                    });
                    props_->setCellWidget(r, 1, keep);
                }
                break;
            }
        }
        if (!b.buildError.empty() && b.buildError.find(o->ref()) != std::string::npos) row(tr("Error"), QString::fromStdString(b.buildError));
    } else if (const FreeSurface* f = b.model->surface(it)) {
        row(tr("Surface"), QString::fromStdString(f->name));
        row(tr("Reference"), QString::fromStdString(f->ref()));
        row(tr("Poles"), QString("%1 × %2").arg(f->surface.nu).arg(f->surface.nv));
        row(tr("Size (mm)"), loc.toString(f->surface.width, 'f', 2) + " × " + loc.toString(f->surface.height, 'f', 2));
        row(tr("Fit to the scan"), tr("rms %1 mm, worst %2 mm").arg(loc.toString(f->rms, 'f', 4), loc.toString(f->maxDeviation, 'f', 4)));
        const Vec3d& n = f->surface.normal;
        row(tr("Normal"), loc.toString(n.x(), 'f', 3) + "; " + loc.toString(n.y(), 'f', 3) + "; " + loc.toString(n.z(), 'f', 3));
    }
    props_->resizeColumnsToContents();
    props_->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::onPropertyEdited(int r) {
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b || !b->isPart() || !props_->item(r, 0) || !props_->item(r, 1)) return;
    const QString field = props_->item(r, 0)->data(Qt::UserRole).toString();
    if (field.isEmpty()) return;
    QTableWidgetItem* cell = props_->item(r, 1);
    const std::string it = partItem_.toStdString();
    const std::string ref = b->ref();
    // Put the shown value back if the edit is refused: the document is the truth.
    auto restore = [this]() { QMetaObject::invokeMethod(this, [this]() { refreshProperties(); }, Qt::QueuedConnection); };
    if (it.rfind("p:", 0) == 0) {
        bool ok = false;
        const json v = valueFromText(cell->text(), &ok);
        if (!ok || !v.is_number()) { restore(); return; }
        run("part.parameter", {{"body", ref}, {"name", it.substr(2)}, {"value", v}});
        return;
    }
    if (it.front() == 's') {
        if (field == "name") {
            run("part.edit_sketch", {{"body", ref}, {"sketch", it}, {"name", cell->text().trimmed().toStdString()}});
            return;
        }
        const Sketch* sk = b->model->sketch(it);
        if (!sk) return;
        const QStringList parts = cell->text().split(';');
        if (parts.size() != 3) { restore(); return; }
        double v[3];
        for (int k = 0; k < 3; ++k) {
            bool ok = false;
            v[k] = QLocale().toDouble(parts[k].trimmed(), &ok);
            if (!ok) v[k] = QLocale::c().toDouble(parts[k].trimmed(), &ok);
            if (!ok) { restore(); return; }
        }
        const auto& pl = sk->profile.plane;
        json plane{{"origin", {pl.origin.x(), pl.origin.y(), pl.origin.z()}}, {"normal", {pl.normal.x(), pl.normal.y(), pl.normal.z()}}, {"xDir", {pl.xDir.x(), pl.xDir.y(), pl.xDir.z()}}};
        plane[field.toStdString()] = {v[0], v[1], v[2]};
        run("part.edit_sketch", {{"body", ref}, {"sketch", it}, {"plane", plane}});
        return;
    }
    json in{{"body", ref}, {"operation", it}};
    if (field == "name") in["name"] = cell->text().trimmed().toStdString();
    else {
        bool ok = false;
        const json v = valueFromText(cell->text(), &ok);
        if (!ok) { restore(); return; }
        in[field.toStdString()] = v;
    }
    run("part.edit", in);
}

void MainWindow::showTreeMenu(const QPoint& pos) {
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (!item) return;
    tree_->setCurrentItem(item);
    const QString it = item->data(0, Qt::UserRole + 1).toString();
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b) return;
    QMenu menu(this);
    const std::string ref = b->ref();
    if (it.isEmpty()) {
        menu.addAction(tr("Rename…"), this, [this]() { runFromUi("body.rename"); });
        menu.addAction(b->opacity < 1.f ? tr("Make opaque") : tr("See through"), this, [this, ref, op = b->opacity]() {
            run("body.set_opacity", {{"body", ref}, {"opacity", op < 1.f ? 1.0 : 0.4}});
        });
        if (b->deviation) menu.addAction(tr("Remove deviation colours"), this, [this, ref]() { run("body.clear_deviation", {{"body", ref}}); });
        if (b->isPart()) {
            menu.addSeparator();
            menu.addAction(tr("Compare with a scan…"), this, &MainWindow::compareWithScan);
            menu.addAction(QString::fromStdString(registry_.find("part.export_step")->title.in(lang_.toStdString())) + "…", this, [this]() { runFromUi("part.export_step"); });
        }
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, ref]() { run("body.delete", {{"body", ref}}); });
    } else if (it.startsWith("p:")) {
        menu.addAction(tr("Change value…"), this, [this, ref, name = it.mid(2), b]() {
            bool ok = false;
            const double now = b->model->parameters.count(name.toStdString()) ? b->model->parameters.at(name.toStdString()) : 0.0;
            const double v = QInputDialog::getDouble(this, tr("Parameter"), name, now, -1e6, 1e6, 4, &ok);
            if (ok) run("part.parameter", {{"body", ref}, {"name", name.toStdString()}, {"value", v}});
        });
    } else if (it.startsWith("sf")) {
        menu.addAction(tr("Cut the part by it, keeping what is below"), this, [this, ref, it]() { run("part.cut_by_surface", {{"body", ref}, {"surface", it.toStdString()}, {"keep", "below"}}); });
        menu.addAction(tr("Cut the part by it, keeping what is above"), this, [this, ref, it]() { run("part.cut_by_surface", {{"body", ref}, {"surface", it.toStdString()}, {"keep", "above"}}); });
        menu.addAction(tr("Thicken…"), this, [this, ref, it]() {
            bool ok = false;
            const double t = QInputDialog::getDouble(this, tr("Thicken a surface"), tr("Thickness (mm; negative: the other side):"), 1.0, -1000, 1000, 3, &ok);
            if (ok) run("part.thicken", {{"body", ref}, {"surface", it.toStdString()}, {"thickness", t}});
        });
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, ref, it]() { run("part.delete", {{"body", ref}, {"item", it.toStdString()}}); });
    } else if (it.startsWith('s')) {
        menu.addAction(tr("Edit sketch…"), this, [this, id = it.mid(1).toULongLong()]() { editSketch(selected_, id); });
        menu.addAction(tr("Extrude…"), this, [this]() { runFromUi("part.extrude"); });
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, ref, it]() { run("part.delete", {{"body", ref}, {"item", it.toStdString()}}); });
    } else if (const Operation* o = b->model->operation(it.toStdString())) {
        const auto& ops = b->model->operations;
        std::size_t index = 0;
        while (index < ops.size() && ops[index].id != o->id) ++index;
        if (o->kind == OpKind::Extrude || o->kind == OpKind::Revolve)
            menu.addAction(tr("Edit its sketch…"), this, [this, sk = o->sketch]() { editSketch(selected_, sk); });
        menu.addAction(o->suppressed ? tr("Unsuppress") : tr("Suppress"), this, [this, ref, it, sup = o->suppressed]() {
            run("part.edit", {{"body", ref}, {"operation", it.toStdString()}, {"suppressed", !sup}});
        });
        QAction* up = menu.addAction(tr("Move up"), this, [this, ref, it, index]() { run("part.move", {{"body", ref}, {"operation", it.toStdString()}, {"position", static_cast<long long>(index)}}); });
        up->setEnabled(index > 0);
        QAction* down = menu.addAction(tr("Move down"), this, [this, ref, it, index]() { run("part.move", {{"body", ref}, {"operation", it.toStdString()}, {"position", static_cast<long long>(index + 2)}}); });
        down->setEnabled(index + 1 < ops.size());
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, ref, it]() { run("part.delete", {{"body", ref}, {"item", it.toStdString()}}); });
    }
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void MainWindow::editSketch(std::uint64_t bodyId, std::uint64_t sketchId) {
    auto b = doc_.findBody("b" + std::to_string(bodyId));
    if (!b || !b->isPart()) return;
    const Sketch* sk = b->model->sketch("s" + std::to_string(sketchId));
    if (!sk) return;
    SketchEditor ed(sk->profile, QString::fromStdString(sk->name), doc_.bodies(), lang_, this);
    if (ed.exec() != QDialog::Accepted) return;
    json loops = profileToJson(ed.profile())["loops"];
    run("part.edit_sketch", {{"body", b->ref()}, {"sketch", sk->ref()}, {"loops", loops}});
}

void MainWindow::newSketchOnFace() {
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b || !b->isPart() || pickedFaces_.size() != 1) {
        QMessageBox::information(this, tr("OpenRevers"), tr("Pick one flat face of a part first (click on it in the view)."));
        return;
    }
    const ShapeInfo f = b->solid.faceInfo(pickedFaces_.front());
    if (f.type != "plane") {
        QMessageBox::information(this, tr("OpenRevers"), tr("A sketch needs a flat face."));
        return;
    }
    Profile p;
    p.plane.origin = f.origin;
    p.plane.normal = f.axis;
    // x along the world axis that lies most in the face.
    Vec3d x = Vec3d::UnitX();
    if (std::abs(f.axis.dot(x)) > 0.9) x = Vec3d::UnitY();
    p.plane.xDir = x;
    p.plane.normalize();
    SketchEditor ed(p, tr("New sketch"), doc_.bodies(), lang_, this);
    if (ed.exec() != QDialog::Accepted) return;
    const Profile out = ed.profile();
    if (out.loops.empty()) return;
    json j = profileToJson(out);
    run("part.sketch", {{"body", b->ref()}, {"plane", j["plane"]}, {"loops", j["loops"]}}, [this](const json& r) {
        partItem_ = QString::fromStdString(r.value("ref", ""));
        refreshTree();
        refreshProperties();
    });
}

void MainWindow::filletPickedEdges() {
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b || !b->isPart() || pickedEdges_.empty() || !b->view) {
        QMessageBox::information(this, tr("OpenRevers"), tr("Pick the edges to round in the view first (Ctrl+click adds more)."));
        return;
    }
    bool ok = false;
    const double r = QInputDialog::getDouble(this, tr("Round edges"), tr("Radius (mm) for the %1 picked edges:").arg(pickedEdges_.size()), 0.5, 0.001, 1000, 3, &ok);
    if (!ok) return;
    json pts = json::array();
    for (int e : pickedEdges_)
        if (static_cast<std::size_t>(e) < b->view->edgeMiddle.size()) {
            const Vec3d m = b->view->edgeMiddle[static_cast<std::size_t>(e)];
            pts.push_back({m.x(), m.y(), m.z()});
        }
    pickedEdges_.clear();
    run("part.fillet", {{"body", b->ref()}, {"points", pts}, {"radius", r}});
}

void MainWindow::compareWithScan() {
    // The part picked and the scan: the only mesh body, or asked for.
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b || !b->isPart()) {
        runFromUi("part.compare");
        return;
    }
    QStringList scans;
    for (const auto& x : doc_.bodies())
        if (!x.isPart() && !x.mesh->faces.empty()) scans << QString::fromStdString(x.ref() + " · " + x.name);
    if (scans.isEmpty()) {
        QMessageBox::information(this, tr("OpenRevers"), tr("There is no scan to compare with."));
        return;
    }
    bool ok = true;
    const QString scan = scans.size() == 1 ? scans.front() : QInputDialog::getItem(this, tr("Compare with a scan"), tr("Scan:"), scans, 0, false, &ok);
    if (!ok) return;
    const double tol = QInputDialog::getDouble(this, tr("Compare with a scan"), tr("Tolerance (mm):"), 0.2, 0.001, 100, 3, &ok);
    if (!ok) return;
    run("part.compare", {{"body", b->ref()}, {"scan", scan.section(' ', 0, 0).toStdString()}, {"tolerance", tol}}, [this](const json& r) {
        QLocale loc;
        QStringList lines;
        lines << QString::fromStdString(r.value("summary", ""));
        for (const auto& z : r.value("zones", json::array())) {
            const auto& c = z["centre"];
            lines << tr("Zone at %1, %2, %3: %4 % of the scan, %5 mm (%6)")
                         .arg(loc.toString(c[0].get<double>(), 'f', 1), loc.toString(c[1].get<double>(), 'f', 1), loc.toString(c[2].get<double>(), 'f', 1),
                              loc.toString(z["percent"].get<double>(), 'f', 1), loc.toString(z["meanDistance"].get<double>(), 'f', 3), QString::fromStdString(z["side"].get<std::string>()));
        }
        log(tr("Compare"), lines.join("\n"));
        status_->setText(lines.front());
    });
}

void MainWindow::refreshShapes() {
    updatingShapes_ = true;
    shapes_->clear();
    auto b = doc_.findBody(selectedRef().toStdString());
    if (!b || !b->features) {
        updatingShapes_ = false;
        pickedShapes_.clear();
        viewport_->setHighlightedRegions(selected_, {});
        return;
    }
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
    updatingShapes_ = false;
    // Picks that no longer exist (another body, a new segmentation) are dropped.
    QStringList keep;
    if (b->features)
        for (const auto& r : pickedShapes_)
            if (b->features->find(r.toStdString())) keep << r;
    pickedShapes_ = keep;
    syncShapeSelection();
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
        std::optional<std::array<double, 3>> point;
        if (lastPick_.hit() && lastPick_.body == selected_) point = std::array<double, 3>{lastPick_.point.x(), lastPick_.point.y(), lastPick_.point.z()};
        CommandDialog dlg(*c, bodies, selectedRef(), lang_, this, pickedShapes_, point);
        if (dlg.exec() != QDialog::Accepted) return;
        params = dlg.values();
    }
    run(name, params, [this, name](const json& r) {
        if (name == "features.measure") {
            QLocale loc;
            QStringList lines;
            if (r.contains("angleDeg")) lines << tr("Angle: %1°").arg(loc.toString(r["angleDeg"].get<double>(), 'f', 3));
            if (r.contains("distance")) lines << tr("Distance: %1 mm").arg(loc.toString(r["distance"].get<double>(), 'f', 4));
            if (r.contains("axisIntersection")) {
                const auto& p = r["axisIntersection"];
                lines << tr("Axis meets the plane at: %1, %2, %3").arg(loc.toString(p[0].get<double>(), 'f', 3), loc.toString(p[1].get<double>(), 'f', 3), loc.toString(p[2].get<double>(), 'f', 3));
            }
            if (lines.isEmpty()) lines << tr("These two shapes have no simple relation to measure.");
            QMessageBox::information(this, tr("Measure"), lines.join("\n"));
        }
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
