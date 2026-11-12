// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — aligning a scan onto another by pairs of points picked in the
// view: the same detail on each, in the same order (align.point_pairs).
#pragma once

#include "app/Viewport.h"

#include <QDialog>

class QComboBox;
class QListWidget;
class QLabel;
class QPushButton;
class QCheckBox;

namespace orv {

class PairAlignDialog : public QDialog {
    Q_OBJECT
public:
    PairAlignDialog(const std::vector<Body>& bodies, std::uint64_t selected, Viewport* view, QWidget* parent = nullptr);
    /// A click in the view: kept when it falls on one of the two bodies.
    void addPick(const Pick& p);
    void setBodies(const std::vector<Body>& bodies);

signals:
    /// Run align.point_pairs with these inputs.
    void alignRequested(const QString& body, const QString& reference, const std::vector<Vec3d>& points, const std::vector<Vec3d>& referencePoints, bool refine);

private:
    void refresh();
    std::uint64_t idOf(const QComboBox* c) const;
    Viewport* view_;
    QComboBox *moving_, *reference_;
    QListWidget *listMoving_, *listReference_;
    QLabel* status_;
    QPushButton* align_;
    QCheckBox* refine_;
    std::vector<Vec3d> a_, b_;
};

}  // namespace orv
