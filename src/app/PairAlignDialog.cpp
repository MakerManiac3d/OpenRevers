// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/PairAlignDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace orv {

PairAlignDialog::PairAlignDialog(const std::vector<Body>& bodies, std::uint64_t selected, Viewport* view, QWidget* parent) : QDialog(parent), view_(view) {
    setWindowTitle(tr("Align by pairs of points"));
    setModal(false);
    auto* lay = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Click the same detail (a corner, the centre of a hole, a mark) on the scan to move and on the reference, "
                               "in the same order: at least three pairs, spread out. Then Align: the pairs place it and the best fit finishes it."));
    help->setWordWrap(true);
    lay->addWidget(help);
    auto* grid = new QGridLayout();
    moving_ = new QComboBox();
    reference_ = new QComboBox();
    listMoving_ = new QListWidget();
    listReference_ = new QListWidget();
    grid->addWidget(new QLabel(tr("Scan to move (orange)")), 0, 0);
    grid->addWidget(new QLabel(tr("Reference (cyan)")), 0, 1);
    grid->addWidget(moving_, 1, 0);
    grid->addWidget(reference_, 1, 1);
    grid->addWidget(listMoving_, 2, 0);
    grid->addWidget(listReference_, 2, 1);
    lay->addLayout(grid);
    auto* row = new QHBoxLayout();
    auto* undo = new QPushButton(tr("Remove last"));
    auto* clear = new QPushButton(tr("Clear"));
    refine_ = new QCheckBox(tr("Refine with best fit"));
    refine_->setChecked(true);
    row->addWidget(undo);
    row->addWidget(clear);
    row->addStretch();
    row->addWidget(refine_);
    lay->addLayout(row);
    status_ = new QLabel();
    lay->addWidget(status_);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    align_ = box->addButton(tr("Align"), QDialogButtonBox::AcceptRole);
    lay->addWidget(box);
    setBodies(bodies);
    // The selected scan moves; the reference is another one.
    if (const int i = moving_->findData(QVariant::fromValue<qulonglong>(selected)); i >= 0) moving_->setCurrentIndex(i);
    for (int i = 0; i < reference_->count(); ++i)
        if (reference_->itemData(i).toULongLong() != idOf(moving_)) {
            reference_->setCurrentIndex(i);
            break;
        }

    connect(undo, &QPushButton::clicked, this, [this]() {
        // The list with more points (or the moving one, when even) loses its last.
        if (b_.size() > a_.size() || (a_.empty() && !b_.empty())) b_.pop_back();
        else if (!a_.empty()) a_.pop_back();
        refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        a_.clear();
        b_.clear();
        refresh();
    });
    auto changed = [this]() {
        a_.clear();
        b_.clear();
        refresh();
    };
    connect(moving_, &QComboBox::currentIndexChanged, this, changed);
    connect(reference_, &QComboBox::currentIndexChanged, this, changed);
    connect(align_, &QPushButton::clicked, this, [this]() {
        emit alignRequested(QString("b%1").arg(idOf(moving_)), QString("b%1").arg(idOf(reference_)), a_, b_, refine_->isChecked());
        a_.clear();
        b_.clear();
        refresh();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(this, &QDialog::finished, this, [this]() { view_->setPairMarks({}, {}); });
    refresh();
}

void PairAlignDialog::setBodies(const std::vector<Body>& bodies) {
    const std::uint64_t m = idOf(moving_), r = idOf(reference_);
    for (QComboBox* c : {moving_, reference_}) {
        const QSignalBlocker block(c);
        c->clear();
        for (const auto& b : bodies)
            if (!b.isPart()) c->addItem(QString::fromStdString(b.name) + "  (" + QString::fromStdString(b.ref()) + ")", QVariant::fromValue<qulonglong>(b.id));
    }
    auto select = [](QComboBox* c, std::uint64_t id) {
        const int i = c->findData(QVariant::fromValue<qulonglong>(id));
        if (i >= 0) c->setCurrentIndex(i);
    };
    select(moving_, m);
    select(reference_, r);
}

std::uint64_t PairAlignDialog::idOf(const QComboBox* c) const { return c->currentIndex() >= 0 ? c->currentData().toULongLong() : 0; }

void PairAlignDialog::addPick(const Pick& p) {
    if (!p.hit()) return;
    if (p.body == idOf(moving_)) a_.push_back(p.point);
    else if (p.body == idOf(reference_)) b_.push_back(p.point);
    else return;
    refresh();
}

void PairAlignDialog::refresh() {
    auto fill = [](QListWidget* l, const std::vector<Vec3d>& pts) {
        l->clear();
        for (std::size_t i = 0; i < pts.size(); ++i)
            l->addItem(QString("%1:  %2, %3, %4").arg(i + 1).arg(pts[i].x(), 0, 'f', 2).arg(pts[i].y(), 0, 'f', 2).arg(pts[i].z(), 0, 'f', 2));
    };
    fill(listMoving_, a_);
    fill(listReference_, b_);
    const bool same = idOf(moving_) == idOf(reference_);
    const bool ok = !same && a_.size() >= 3 && a_.size() == b_.size();
    align_->setEnabled(ok);
    if (same) status_->setText(tr("Choose two different bodies."));
    else if (a_.size() != b_.size()) status_->setText(tr("Each point needs its pair: %1 on the scan, %2 on the reference.").arg(a_.size()).arg(b_.size()));
    else if (a_.size() < 3) status_->setText(tr("%1 pairs; at least three are needed.").arg(a_.size()));
    else status_->setText(tr("%1 pairs: ready.").arg(a_.size()));
    view_->setPairMarks(a_, b_);
}

}  // namespace orv
