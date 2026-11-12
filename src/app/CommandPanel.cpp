// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CommandPanel.h"

#include "doc/Model.h"

#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace orv {

namespace {

/// The wheel scrolls the panel; it changes a number or a list only when that
/// control has been clicked into (so passing over the form changes nothing).
class WheelGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Wheel) {
            auto* w = qobject_cast<QWidget*>(o);
            if (w && !w->hasFocus()) {
                e->ignore();
                return true;
            }
        }
        return false;
    }
};

WheelGuard* wheelGuard() {
    static auto* g = new WheelGuard(qApp);
    return g;
}

template <class W>
W* guarded(W* w) {
    w->setFocusPolicy(Qt::StrongFocus);
    w->installEventFilter(wheelGuard());
    return w;
}

QDoubleSpinBox* coordinate() {
    auto* sp = guarded(new QDoubleSpinBox());
    sp->setRange(-1e7, 1e7);
    sp->setDecimals(4);
    sp->setMinimumWidth(70);
    return sp;
}

/// Three (or two) numbers in a row.
QWidget* row(std::vector<QDoubleSpinBox*>& boxes, int n) {
    auto* w = new QWidget();
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < n; ++i) {
        boxes.push_back(coordinate());
        h->addWidget(boxes.back());
    }
    return w;
}

json readXyz(const std::vector<QDoubleSpinBox*>& b) {
    json a = json::array();
    for (auto* s : b) a.push_back(s->value());
    return a;
}

void writeXyz(const std::vector<QDoubleSpinBox*>& b, const json& v) {
    if (!v.is_array()) return;
    for (std::size_t i = 0; i < b.size() && i < v.size(); ++i)
        if (v[i].is_number()) b[i]->setValue(v[i].get<double>());
}

Vec3d perpendicular(const Vec3d& n) {
    const Vec3d a = std::abs(n.x()) < 0.9 ? Vec3d::UnitX() : Vec3d::UnitY();
    return (a - n * a.dot(n)).normalized();
}

QString itemText(const std::string& name, const std::string& ref) {
    return name.empty() || name == ref ? QString::fromStdString(ref) : QString::fromStdString(name + "  (" + ref + ")");
}

}  // namespace

CommandPanel::CommandPanel(QWidget* parent) : QWidget(parent) {
    layout_ = new QVBoxLayout(this);
    title_ = new QLabel();
    QFont f = title_->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    title_->setFont(f);
    layout_->addWidget(title_);
    intro_ = new QLabel();
    intro_->setWordWrap(true);
    layout_->addWidget(intro_);
    arms_ = new QButtonGroup(this);
    arms_->setExclusive(false);
    auto* buttons = new QHBoxLayout();
    live_ = new QCheckBox(tr("See it live"));
    live_->setToolTip(tr("Show what it would do (in amber) while you change the values; nothing changes until Apply"));
    buttons->addWidget(live_);
    apply_ = new QPushButton(tr("Apply"));
    apply_->setDefault(true);
    auto* close = new QPushButton(tr("Close"));
    buttons->addStretch();
    buttons->addWidget(apply_);
    buttons->addWidget(close);
    layout_->addLayout(buttons);
    layout_->addStretch();
    connect(apply_, &QPushButton::clicked, this, [this]() {
        if (command_) emit applyRequested(QString::fromStdString(command_->name), values());
    });
    connect(close, &QPushButton::clicked, this, &CommandPanel::closeRequested);
    connect(live_, &QCheckBox::toggled, this, &CommandPanel::valuesChanged);
    // Any change of any control: compared a few times a second.
    poll_ = new QTimer(this);
    poll_->setInterval(250);
    connect(poll_, &QTimer::timeout, this, [this]() {
        if (!command_ || !isVisible()) return;
        json v = values();
        if (v == last_) return;
        last_ = std::move(v);
        emit valuesChanged();
    });
    poll_->start();
}

bool CommandPanel::live() const { return live_->isChecked(); }

std::vector<Viewport::Handle> CommandPanel::handles() const {
    std::vector<Viewport::Handle> out;
    const json v = values();
    for (const auto& f : fields_) {
        if (!f.handle || !v.contains(f.key)) continue;
        const json& p = f.kind == FieldKind::Plane ? v[f.key]["origin"] : v[f.key];
        if (!p.is_array() || p.size() != 3) continue;
        out.push_back({QString::fromStdString(f.key), Vec3d(p[0].get<double>(), p[1].get<double>(), p[2].get<double>()), QColor(255, 170, 40)});
    }
    return out;
}

void CommandPanel::moveHandle(const QString& id, const Vec3d& pos) {
    for (auto& f : fields_) {
        if (QString::fromStdString(f.key) != id || !f.write) continue;
        const json p = json::array({pos.x(), pos.y(), pos.z()});
        if (f.kind == FieldKind::Plane) {
            json pl = f.read();
            if (!pl.is_object()) pl = json::object();
            pl["origin"] = p;
            f.write(pl);
        } else {
            f.write(p);
        }
    }
}

void CommandPanel::open(const Command& command, const Context& context, const json& initial) {
    command_ = &command;
    ctx_ = context;
    live_->setChecked(false);
    build(initial);
    last_ = json();
}

void CommandPanel::refresh(const Context& context) {
    if (!command_) return;
    const json keep = values();
    ctx_ = context;
    ctx_.lastPick = Pick{};  // a refresh does not prefill from an old click
    build(keep);
}

const Body* CommandPanel::findBody(const QString& ref) const {
    for (const auto& b : ctx_.bodies)
        if (QString::fromStdString(b.ref()) == ref) return &b;
    return nullptr;
}

QString CommandPanel::bodyOf(const std::string& ofKey) const {
    const auto it = bodyBoxes_.find(ofKey);
    if (it == bodyBoxes_.end()) return ctx_.selectedBody;
    return it->second->currentData().toString();
}

bool CommandPanel::wantsPick() const {
    for (const auto& f : fields_)
        if (f.arm && f.arm->isChecked()) return true;
    return false;
}

void CommandPanel::takePick(const Pick& p) {
    for (auto& f : fields_) {
        if (!f.arm || !f.arm->isChecked() || !f.pick) continue;
        if (p.hit()) f.pick(p);
        if (!f.keepArmed) f.arm->setChecked(false);
        return;
    }
}

json CommandPanel::values() const {
    json out = json::object();
    for (const auto& f : fields_) {
        json v = f.read();
        if (!v.is_null()) out[f.key] = v;
    }
    return out;
}

void CommandPanel::build(const json& initial) {
    // Clear the old form.
    for (auto* b : arms_->buttons()) arms_->removeButton(b);
    fields_.clear();
    bodyBoxes_.clear();
    dependents_.clear();
    if (form_) {
        layout_->removeWidget(form_);
        form_->deleteLater();
        form_ = nullptr;
    }
    if (!command_) return;
    const std::string l = ctx_.lang.toStdString();
    title_->setText(QString::fromStdString(command_->title.in(l)));
    // The first sentence; the whole description when the pointer rests on it.
    const QString d = QString::fromStdString(command_->description.in(l));
    const qsizetype stop = d.indexOf(QRegularExpression(QStringLiteral("[.:](\\s|$)")));
    intro_->setText(stop > 0 && stop < d.size() - 1 ? d.left(stop + 1) + QStringLiteral(" …") : d);
    intro_->setToolTip(d);
    form_ = new QWidget();
    auto* form = new QFormLayout(form_);
    form->setContentsMargins(0, 4, 0, 4);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout_->insertWidget(2, form_);

    const json& props = command_->schema.value("properties", json::object());
    const std::vector<std::string> required = command_->schema.value("required", std::vector<std::string>{});
    // Body fields first, so the fields that depend on one find it.
    std::vector<std::string> order;
    for (auto it = props.begin(); it != props.end(); ++it)
        if (fieldKind(it.value()) == FieldKind::Body) order.push_back(it.key());
    for (auto it = props.begin(); it != props.end(); ++it)
        if (fieldKind(it.value()) != FieldKind::Body) order.push_back(it.key());
    fields_.reserve(order.size());
    for (const auto& key : order) {
        const json& s = props[key];
        Field f;
        f.key = key;
        f.kind = fieldKind(s);
        const bool req = std::find(required.begin(), required.end(), key) != required.end();
        QWidget* w = makeField(f, s, req);
        if (!w) continue;
        QString label = QString::fromStdString(l == "es" && s.contains("x-es") ? s["x-es"].get<std::string>() : s.value("description", key));
        if (s.contains("x-unit")) label += QString(" (%1)").arg(QString::fromStdString(s["x-unit"]));
        auto* lab = new QLabel(label + (req ? " *" : ""));
        lab->setWordWrap(true);
        lab->setToolTip(QString::fromStdString(key));
        w->setToolTip(QString::fromStdString(key));
        form->addRow(lab, w);
        if (f.arm) arms_->addButton(f.arm);
        fields_.push_back(std::move(f));
    }
    for (auto& refill : dependents_) refill();
    for (auto& f : fields_)
        if (initial.contains(f.key) && f.write) f.write(initial[f.key]);
    // One armed field at a time.
    for (auto* b : arms_->buttons())
        connect(b, &QAbstractButton::toggled, this, [this, b](bool on) {
            if (!on) return;
            for (auto* o : arms_->buttons())
                if (o != b) o->setChecked(false);
        });
}

QWidget* CommandPanel::makeField(Field& f, const json& s, bool required) {
    const std::string key = f.key;
    const std::string of = s.value("x-of", "body");
    auto armButton = [&f](const QString& text, const QString& tip) {
        auto* b = new QToolButton();
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        f.arm = b;
        return b;
    };
    auto withArm = [](QWidget* main, QWidget* arm) {
        auto* w = new QWidget();
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(main, 1);
        h->addWidget(arm);
        return w;
    };

    switch (f.kind) {
        case FieldKind::Body: {
            auto* box = guarded(new QComboBox());
            if (!required) box->addItem(QString(), QString());
            for (const auto& b : ctx_.bodies) box->addItem(itemText(b.name, b.ref()), QString::fromStdString(b.ref()));
            int i = box->findData(ctx_.selectedBody);
            // A second body field (a reference, a scan) starts on another body.
            if (!bodyBoxes_.empty() && i >= 0 && box->count() > 1) i = (i + 1) % box->count();
            if (i >= 0) box->setCurrentIndex(i);
            bodyBoxes_[key] = box;
            connect(box, &QComboBox::currentIndexChanged, this, [this]() {
                for (auto& refill : dependents_) refill();
            });
            f.read = [box]() { return box->currentData().toString().isEmpty() ? json(nullptr) : json(box->currentData().toString().toStdString()); };
            f.write = [box](const json& v) {
                if (!v.is_string()) return;
                const int k = box->findData(QString::fromStdString(v.get<std::string>()));
                if (k >= 0) box->setCurrentIndex(k);
            };
            f.pick = [box](const Pick& p) {
                const int k = box->findData(QString("b%1").arg(p.body));
                if (k >= 0) box->setCurrentIndex(k);
            };
            return withArm(box, armButton(QStringLiteral("⌖"), tr("Choose by clicking it in the view")));
        }
        case FieldKind::Feature:
        case FieldKind::Sketch:
        case FieldKind::Operation:
        case FieldKind::Surface:
        case FieldKind::PartItem:
        case FieldKind::Parameter: {
            auto* box = guarded(new QComboBox());
            box->setEditable(true);  // a name, or a word such as "all"
            const FieldKind kind = f.kind;
            const bool optional = !required;
            // Which shape of the picked ones this field takes (the n-th feature field).
            int nth = 0;
            for (const auto& g : fields_)
                if (g.kind == FieldKind::Feature) ++nth;
            auto refill = [this, box, kind, of, optional, nth]() {
                const QString keep = box->currentData().toString().isEmpty() ? box->currentText() : box->currentData().toString();
                const QSignalBlocker block(box);
                box->clear();
                if (optional) box->addItem(QString(), QString());
                const Body* b = findBody(bodyOf(of));
                if (b && kind == FieldKind::Feature && b->features)
                    for (const auto& ft : b->features->list) box->addItem(itemText(ft.name, ft.ref()), QString::fromStdString(ft.ref()));
                if (b && b->model) {
                    const Model& m = *b->model;
                    if (kind == FieldKind::Sketch || kind == FieldKind::PartItem)
                        for (const auto& sk : m.sketches) box->addItem(itemText(sk.name, sk.ref()), QString::fromStdString(sk.ref()));
                    if (kind == FieldKind::Operation || kind == FieldKind::PartItem)
                        for (const auto& o : m.operations) box->addItem(itemText(o.name, o.ref()), QString::fromStdString(o.ref()));
                    if (kind == FieldKind::Surface || kind == FieldKind::PartItem)
                        for (const auto& sf : m.surfaces) box->addItem(itemText(sf.name, sf.ref()), QString::fromStdString(sf.ref()));
                    if (kind == FieldKind::Parameter)
                        for (const auto& [name, v] : m.parameters) box->addItem(QString::fromStdString(name), QString::fromStdString(name));
                }
                int k = box->findData(keep);
                if (k < 0 && kind == FieldKind::Feature && nth < ctx_.pickedShapes.size()) k = box->findData(ctx_.pickedShapes[nth]);
                if (k < 0 && !keep.isEmpty()) box->setEditText(keep);
                else if (k >= 0) box->setCurrentIndex(k);
                else if (optional) box->setCurrentIndex(0);
            };
            dependents_.push_back(refill);
            f.read = [box]() {
                const int i = box->currentIndex();
                if (i >= 0 && box->itemText(i) == box->currentText() && !box->itemData(i).toString().isEmpty()) return json(box->itemData(i).toString().toStdString());
                const QString t = box->currentText().trimmed();
                return t.isEmpty() ? json(nullptr) : json(t.toStdString());
            };
            f.write = [box](const json& v) {
                if (!v.is_string()) return;
                const QString t = QString::fromStdString(v.get<std::string>());
                const int k = box->findData(t);
                if (k >= 0) box->setCurrentIndex(k);
                else box->setEditText(t);
            };
            return box;
        }
        case FieldKind::BodyList: {
            auto* list = new QListWidget();
            list->setMaximumHeight(120);
            for (const auto& b : ctx_.bodies) {
                auto* it = new QListWidgetItem(itemText(b.name, b.ref()), list);
                it->setData(Qt::UserRole, QString::fromStdString(b.ref()));
                it->setCheckState(QString::fromStdString(b.ref()) == ctx_.selectedBody ? Qt::Checked : Qt::Unchecked);
            }
            f.read = [list]() {
                json a = json::array();
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->checkState() == Qt::Checked) a.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
                return a.empty() ? json(nullptr) : a;
            };
            f.write = [list](const json& v) {
                if (!v.is_array()) return;
                for (int i = 0; i < list->count(); ++i) {
                    const std::string r = list->item(i)->data(Qt::UserRole).toString().toStdString();
                    list->item(i)->setCheckState(std::find(v.begin(), v.end(), json(r)) != v.end() ? Qt::Checked : Qt::Unchecked);
                }
            };
            f.pick = [list](const Pick& p) {
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->data(Qt::UserRole).toString() == QString("b%1").arg(p.body))
                        list->item(i)->setCheckState(list->item(i)->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            };
            f.keepArmed = true;
            return withArm(list, armButton(QStringLiteral("⌖"), tr("Tick bodies by clicking them in the view")));
        }
        case FieldKind::SketchList: {
            auto* list = new QListWidget();
            list->setMaximumHeight(140);
            list->setDragDropMode(QAbstractItemView::InternalMove);
            list->setToolTip(tr("Tick the sketches and drag them into order"));
            auto refill = [this, list, of]() {
                json keep = json::array();
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->checkState() == Qt::Checked) keep.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
                list->clear();
                const Body* b = findBody(bodyOf(of));
                if (!b || !b->model) return;
                // The ticked ones first, in their order; then the rest.
                std::vector<const Sketch*> sks;
                for (const auto& k : keep)
                    for (const auto& sk : b->model->sketches)
                        if (sk.ref() == k.get<std::string>()) sks.push_back(&sk);
                for (const auto& sk : b->model->sketches)
                    if (std::find(sks.begin(), sks.end(), &sk) == sks.end()) sks.push_back(&sk);
                for (const Sketch* sk : sks) {
                    auto* it = new QListWidgetItem(itemText(sk->name, sk->ref()), list);
                    it->setData(Qt::UserRole, QString::fromStdString(sk->ref()));
                    it->setFlags(it->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
                    it->setCheckState(std::find(keep.begin(), keep.end(), json(sk->ref())) != keep.end() ? Qt::Checked : Qt::Unchecked);
                }
            };
            dependents_.push_back(refill);
            f.read = [list]() {
                json a = json::array();
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->checkState() == Qt::Checked) a.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
                return a.empty() ? json(nullptr) : a;
            };
            f.write = [list](const json& v) {
                if (!v.is_array()) return;
                int place = 0;
                for (const auto& r : v) {
                    for (int i = 0; i < list->count(); ++i) {
                        if (list->item(i)->data(Qt::UserRole).toString().toStdString() != r.get<std::string>()) continue;
                        auto* it = list->takeItem(i);
                        it->setCheckState(Qt::Checked);
                        list->insertItem(place++, it);
                        break;
                    }
                }
            };
            return list;
        }
        case FieldKind::OpenPath:
        case FieldKind::SavePath:
        case FieldKind::Folder: {
            auto* edit = new QLineEdit();
            auto* browse = new QToolButton();
            browse->setText("…");
            const QString filter = QString::fromStdString(s.value("x-filter", ""));
            const FieldKind kind = f.kind;
            connect(browse, &QToolButton::clicked, this, [this, edit, filter, kind]() {
                QString p;
                if (kind == FieldKind::Folder) p = QFileDialog::getExistingDirectory(this, QString(), edit->text());
                else if (kind == FieldKind::SavePath) p = QFileDialog::getSaveFileName(this, QString(), edit->text(), filter);
                else p = QFileDialog::getOpenFileName(this, QString(), edit->text(), filter);
                if (!p.isEmpty()) edit->setText(p);
            });
            f.read = [edit]() { return edit->text().isEmpty() ? json(nullptr) : json(edit->text().toStdString()); };
            f.write = [edit](const json& v) {
                if (v.is_string()) edit->setText(QString::fromStdString(v.get<std::string>()));
            };
            return withArm(edit, browse);
        }
        case FieldKind::Choice: {
            auto* box = guarded(new QComboBox());
            for (const auto& e : s["enum"]) box->addItem(QString::fromStdString(e.get<std::string>()));
            box->setCurrentText(QString::fromStdString(s.value("default", "")));
            f.read = [box]() { return json(box->currentText().toStdString()); };
            f.write = [box](const json& v) {
                if (v.is_string()) box->setCurrentText(QString::fromStdString(v.get<std::string>()));
            };
            return box;
        }
        case FieldKind::Text: {
            auto* edit = new QLineEdit();
            if (s.contains("default") && s["default"].is_string()) edit->setText(QString::fromStdString(s["default"].get<std::string>()));
            f.read = [edit]() { return edit->text().isEmpty() ? json(nullptr) : json(edit->text().toStdString()); };
            f.write = [edit](const json& v) {
                if (v.is_string()) edit->setText(QString::fromStdString(v.get<std::string>()));
            };
            return edit;
        }
        case FieldKind::Number: {
            auto* spin = guarded(new QDoubleSpinBox());
            const double mn = s.value("minimum", -1e9), mx = s.value("maximum", 1e9), def = s.value("default", 0.0);
            spin->setRange(mn, mx);
            const double mag = std::max({std::abs(def), std::abs(mn), 1e-6});
            spin->setDecimals(mag < 0.01 ? 6 : 4);
            spin->setSingleStep(def > 0 ? def / 10.0 : (mx - mn) / 1000.0);
            spin->setValue(def);
            f.read = [spin]() { return json(spin->value()); };
            f.write = [spin](const json& v) {
                if (v.is_number()) spin->setValue(v.get<double>());
            };
            return spin;
        }
        case FieldKind::Integer: {
            auto* spin = guarded(new QSpinBox());
            spin->setRange(static_cast<int>(std::max<long long>(s.value("minimum", 0LL), INT32_MIN)), static_cast<int>(std::min<long long>(s.value("maximum", 1000000LL), INT32_MAX)));
            spin->setValue(static_cast<int>(s.value("default", 0LL)));
            f.read = [spin]() { return json(spin->value()); };
            f.write = [spin](const json& v) {
                if (v.is_number()) spin->setValue(v.get<int>());
            };
            return spin;
        }
        case FieldKind::Boolean: {
            auto* check = new QCheckBox();
            check->setChecked(s.value("default", false));
            f.read = [check]() { return json(check->isChecked()); };
            f.write = [check](const json& v) {
                if (v.is_boolean()) check->setChecked(v.get<bool>());
            };
            return check;
        }
        case FieldKind::Value: {
            // A number or a parameter's name: the part's parameters to choose from.
            auto* box = guarded(new QComboBox());
            box->setEditable(true);
            if (s.contains("default")) box->setEditText(s["default"].is_number() ? QLocale().toString(s["default"].get<double>()) : QString::fromStdString(s["default"].dump()));
            auto refill = [this, box]() {
                const QString keep = box->currentText();
                const QSignalBlocker block(box);
                box->clear();
                if (const Body* b = findBody(bodyOf("body")); b && b->model)
                    for (const auto& [name, v] : b->model->parameters) box->addItem(QString::fromStdString(name));
                box->setEditText(keep);
            };
            dependents_.push_back(refill);
            f.read = [box]() {
                const QString t = box->currentText().trimmed();
                if (t.isEmpty()) return json(nullptr);
                bool ok = false;
                const double v = QLocale().toDouble(t, &ok);
                if (ok) return json(v);
                const double v2 = t.toDouble(&ok);
                return ok ? json(v2) : json(t.toStdString());
            };
            f.write = [box](const json& v) {
                if (v.is_number()) box->setEditText(QLocale().toString(v.get<double>()));
                else if (v.is_string()) box->setEditText(QString::fromStdString(v.get<std::string>()));
            };
            return box;
        }
        case FieldKind::Point:
        case FieldKind::Point2: {
            auto boxes = std::make_shared<std::vector<QDoubleSpinBox*>>();
            QWidget* r = row(*boxes, f.kind == FieldKind::Point ? 3 : 2);
            const bool direction = f.kind == FieldKind::Point && !s.contains("x-unit");  // a direction: the face's normal
            if (f.kind == FieldKind::Point && !direction && ctx_.lastPick.hit() &&
                (key == "at" || key == "about" || key == "centre" || key == "point" || key == "origin" || key == "axisOrigin")) {
                const Vec3d& p = ctx_.lastPick.point;
                writeXyz(*boxes, json::array({p.x(), p.y(), p.z()}));
            }
            f.read = [boxes]() { return readXyz(*boxes); };
            f.write = [boxes](const json& v) { writeXyz(*boxes, v); };
            if (f.kind == FieldKind::Point2) return r;
            f.handle = !direction;
            f.pick = [boxes, direction](const Pick& p) {
                const Vec3d v = direction ? p.normal : p.point;
                writeXyz(*boxes, json::array({v.x(), v.y(), v.z()}));
            };
            return withArm(r, armButton(QStringLiteral("⌖"), direction ? tr("Take the normal of the face clicked in the view") : tr("Take the point clicked in the view")));
        }
        case FieldKind::PointList: {
            auto* list = new QListWidget();
            list->setMaximumHeight(110);
            auto pts = std::make_shared<std::vector<Vec3d>>();
            auto show = [list, pts]() {
                list->clear();
                for (std::size_t i = 0; i < pts->size(); ++i)
                    list->addItem(QString("%1:  %2, %3, %4").arg(i + 1).arg((*pts)[i].x(), 0, 'f', 2).arg((*pts)[i].y(), 0, 'f', 2).arg((*pts)[i].z(), 0, 'f', 2));
            };
            auto* clear = new QToolButton();
            clear->setText(tr("Clear"));
            connect(clear, &QToolButton::clicked, this, [pts, show]() { pts->clear(); show(); });
            auto* undo = new QToolButton();
            undo->setText(tr("Remove last"));
            connect(undo, &QToolButton::clicked, this, [pts, show]() { if (!pts->empty()) pts->pop_back(); show(); });
            f.read = [pts]() {
                if (pts->empty()) return json(nullptr);
                json a = json::array();
                for (const auto& p : *pts) a.push_back({p.x(), p.y(), p.z()});
                return a;
            };
            f.write = [pts, show](const json& v) {
                if (!v.is_array()) return;
                pts->clear();
                for (const auto& p : v)
                    if (p.is_array() && p.size() == 3) pts->emplace_back(p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
                show();
            };
            f.pick = [pts, show](const Pick& p) { pts->push_back(p.point); show(); };
            f.keepArmed = true;
            auto* w = new QWidget();
            auto* v = new QVBoxLayout(w);
            v->setContentsMargins(0, 0, 0, 0);
            v->addWidget(list);
            auto* h = new QHBoxLayout();
            h->addWidget(armButton(tr("Add clicks"), tr("Each click in the view adds a point, in order")));
            h->addWidget(undo);
            h->addWidget(clear);
            h->addStretch();
            v->addLayout(h);
            return w;
        }
        case FieldKind::Plane: {
            auto* group = new QGroupBox();
            group->setCheckable(!required);
            group->setChecked(required);
            group->setTitle(required ? QString() : tr("Give a plane (otherwise the default)"));
            auto* g = new QFormLayout(group);
            auto o = std::make_shared<std::vector<QDoubleSpinBox*>>(), n = std::make_shared<std::vector<QDoubleSpinBox*>>(),
                 x = std::make_shared<std::vector<QDoubleSpinBox*>>();
            g->addRow(tr("Origin"), row(*o, 3));
            g->addRow(tr("Normal"), row(*n, 3));
            g->addRow(tr("X direction"), row(*x, 3));
            writeXyz(*n, json::array({0, 0, 1}));
            writeXyz(*x, json::array({1, 0, 0}));
            f.handle = true;
            f.read = [group, o, n, x]() {
                if (group->isCheckable() && !group->isChecked()) return json(nullptr);
                return json{{"origin", readXyz(*o)}, {"normal", readXyz(*n)}, {"xDir", readXyz(*x)}};
            };
            f.write = [group, o, n, x](const json& v) {
                if (!v.is_object()) return;
                group->setChecked(true);
                if (v.contains("origin")) writeXyz(*o, v["origin"]);
                if (v.contains("normal")) writeXyz(*n, v["normal"]);
                if (v.contains("xDir")) writeXyz(*x, v["xDir"]);
            };
            f.pick = [group, o, n, x](const Pick& p) {
                if (p.normal.norm() < 1e-9) return;
                group->setChecked(true);
                const Vec3d nn = p.normal.normalized(), xx = perpendicular(nn);
                writeXyz(*o, json::array({p.point.x(), p.point.y(), p.point.z()}));
                writeXyz(*n, json::array({nn.x(), nn.y(), nn.z()}));
                writeXyz(*x, json::array({xx.x(), xx.y(), xx.z()}));
            };
            g->addRow(QString(), armButton(tr("From a face in the view"), tr("Click a face: the plane goes through the point clicked, along the face")));
            return group;
        }
        case FieldKind::MeshFaces: {
            auto faces = std::make_shared<std::vector<std::uint32_t>>();
            auto* label = new QLabel(tr("Nothing taken"));
            auto* take = new QPushButton(tr("Take the lasso selection"));
            connect(take, &QPushButton::clicked, this, [this, faces, label, of]() {
                if (!selection_) return;
                const auto [body, sel] = selection_();
                if (QString("b%1").arg(body) != bodyOf(of)) {
                    label->setText(tr("The selection is on another body"));
                    return;
                }
                *faces = sel;
                label->setText(tr("%1 taken").arg(faces->size()));
            });
            f.read = [faces]() {
                if (faces->empty()) return json(nullptr);
                json a = json::array();
                for (auto i : *faces) a.push_back(i);
                return a;
            };
            f.write = [faces, label](const json& v) {
                if (!v.is_array()) return;
                faces->clear();
                for (const auto& i : v) faces->push_back(i.get<std::uint32_t>());
                label->setText(tr("%1 taken").arg(faces->size()));
            };
            return withArm(label, take);
        }
        case FieldKind::IndexList: {
            auto* edit = new QLineEdit();
            edit->setPlaceholderText(tr("e.g. 0, 2, 3"));
            f.read = [edit]() {
                json a = json::array();
                static const QRegularExpression num(QStringLiteral("\\d+"));
                auto it = num.globalMatch(edit->text());
                while (it.hasNext()) a.push_back(it.next().captured(0).toInt());
                return a.empty() ? json(nullptr) : a;
            };
            f.write = [edit](const json& v) {
                if (!v.is_array()) return;
                QStringList parts;
                for (const auto& i : v) parts << QString::number(i.get<long long>());
                edit->setText(parts.join(", "));
            };
            return edit;
        }
        case FieldKind::Loops: {
            auto* b = new QPushButton(tr("Draw in the sketch editor…"));
            connect(b, &QPushButton::clicked, this, [this]() {
                if (command_) emit sketchEditorRequested(QString::fromStdString(command_->name), values());
            });
            f.read = []() { return json(nullptr); };  // the editor sends its own command
            return b;
        }
        case FieldKind::Unsupported:
            break;
    }
    return nullptr;
}

}  // namespace orv
