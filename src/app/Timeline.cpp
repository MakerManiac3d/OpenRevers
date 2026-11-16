// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Timeline.h"

#include "app/Icons.h"
#include "doc/Model.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>

#include <algorithm>
#include <string>
#include <vector>

namespace orv {

namespace {

struct Step {
    std::uint64_t id;  // the model's counter: the order steps were made in
    QString ref, name, icon, tip;
    bool suppressed{false}, failed{false}, isOperation{false}, rolledBack{false};
};

QString iconFor(OpKind k) {
    switch (k) {
        case OpKind::Extrude: return "part.extrude";
        case OpKind::Revolve: return "part.revolve";
        case OpKind::Fillet: return "part.fillet";
        case OpKind::Import: return "part.import_step";
        case OpKind::Loft: return "part.loft";
        case OpKind::Pattern: return "part.pattern";
        case OpKind::SurfaceCut: return "part.cut_by_surface";
        case OpKind::Thicken: return "part.thicken";
        case OpKind::Chamfer: return "part.chamfer";
        case OpKind::Hole: return "part.hole";
        case OpKind::Mirror: return "part.mirror";
    }
    return "part.edit";
}

}  // namespace

Timeline::Timeline(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 2, 6, 2);
    title_ = new QLabel();
    title_->setMinimumWidth(120);
    row->addWidget(title_);
    list_ = new QListWidget();
    list_->setViewMode(QListView::IconMode);
    list_->setFlow(QListView::LeftToRight);
    list_->setWrapping(false);
    list_->setMovement(QListView::Static);
    list_->setIconSize(QSize(28, 28));
    list_->setGridSize(QSize(96, 64));
    list_->setUniformItemSizes(true);
    list_->setFixedHeight(84);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    row->addWidget(list_, 1);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (!updating_ && it) emit picked(body_, it->data(Qt::UserRole).toString());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it) emit editRequested(body_, it->data(Qt::UserRole).toString());
    });
    connect(list_, &QListWidget::customContextMenuRequested, this, &Timeline::menuAt);
    showPart(nullptr, {});
}

void Timeline::showPart(const Body* part, const QString& current) {
    updating_ = true;
    list_->clear();
    body_ = part ? part->id : 0;
    if (!part || !part->model) {
        title_->setText(tr("No part yet:\nSolid → New part"));
        title_->setEnabled(false);
        updating_ = false;
        return;
    }
    title_->setEnabled(true);
    title_->setText("<b>" + QString::fromStdString(part->name).toHtmlEscaped() + "</b><br>" +
                    tr("Steps: %1").arg(part->model->sketches.size() + part->model->surfaces.size() + part->model->operations.size()));
    std::vector<Step> steps;
    for (const auto& sk : part->model->sketches)
        steps.push_back({sk.id, QString::fromStdString(sk.ref()), QString::fromStdString(sk.name), "part.sketch", tr("Sketch") + " · " + QString::fromStdString(sk.ref())});
    for (const auto& sf : part->model->surfaces)
        steps.push_back({sf.id, QString::fromStdString(sf.ref()), QString::fromStdString(sf.name), "part.surface_from_scan", tr("Surface") + " · " + QString::fromStdString(sf.ref())});
    bool afterRollback = false;
    for (const auto& op : part->model->operations) {
        Step s{op.id, QString::fromStdString(op.ref()), QString::fromStdString(op.name), iconFor(op.kind),
               QString::fromStdString(op.ref() + " · " + opKindName(op.kind) + " · " + opModeName(op.mode))};
        s.suppressed = op.suppressed;
        s.rolledBack = afterRollback;
        if (part->model->rollback != 0 && op.id == part->model->rollback) afterRollback = true;
        s.failed = !part->buildError.empty() && part->buildError.find(op.ref()) != std::string::npos;
        s.isOperation = true;
        steps.push_back(s);
    }
    std::sort(steps.begin(), steps.end(), [](const Step& a, const Step& b) { return a.id < b.id; });
    for (const auto& s : steps) {
        auto* it = new QListWidgetItem(toolIcon(s.icon), s.name);
        it->setData(Qt::UserRole, s.ref);
        it->setData(Qt::UserRole + 1, s.isOperation);
        it->setData(Qt::UserRole + 2, s.suppressed);
        QString tip = s.tip;
        if (s.suppressed) {
            it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            tip += "\n" + tr("Suppressed: left out of the part");
        }
        if (s.rolledBack) {
            it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            tip += "\n" + tr("Rolled back: left out until the history is applied again");
        }
        if (s.failed) {
            it->setForeground(QColor(200, 40, 40));
            tip += "\n" + QString::fromStdString(part->buildError);
        }
        it->setToolTip(tip + "\n" + tr("Double-click to edit · right-click for more"));
        list_->addItem(it);
        if (s.ref == current) list_->setCurrentItem(it);
    }
    updating_ = false;
}

void Timeline::menuAt(const QPoint& pos) {
    auto* it = list_->itemAt(pos);
    if (!it || !body_) return;
    const QString ref = it->data(Qt::UserRole).toString();
    const bool op = it->data(Qt::UserRole + 1).toBool();
    const bool suppressed = it->data(Qt::UserRole + 2).toBool();
    QMenu menu(this);
    menu.addAction(tr("Edit"), this, [this, ref]() { emit editRequested(body_, ref); });
    if (op)
        menu.addAction(suppressed ? tr("Bring back into the part") : tr("Suppress (leave out of the part)"), this,
                       [this, ref, suppressed]() { emit suppressRequested(body_, ref, !suppressed); });
    if (op) {
        menu.addAction(tr("Roll back to here"), this, [this, ref]() { emit rollbackRequested(body_, ref); });
        menu.addAction(tr("Apply the whole history"), this, [this]() { emit rollbackRequested(body_, "end"); });
    }
    menu.addSeparator();
    menu.addAction(tr("Delete"), this, [this, ref]() { emit deleteRequested(body_, ref); });
    menu.exec(list_->viewport()->mapToGlobal(pos));
}

}  // namespace orv
