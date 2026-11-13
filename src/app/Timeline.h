// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the timeline under the view, as in Fusion: the steps of a
// part (sketches, surfaces, operations) in the order they were made. Click
// to pick a step, double-click to edit it, right-click to suppress or delete.
#pragma once

#include "doc/Document.h"

#include <QWidget>

#include <cstdint>

class QLabel;
class QListWidget;
class QListWidgetItem;

namespace orv {

class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget* parent = nullptr);

    /// Shows the history of the part `partId` (0: none) and marks `current`
    /// (s1, o2, sf3; empty: none).
    void showPart(const Body* part, const QString& current);

signals:
    void picked(std::uint64_t body, const QString& item);
    void editRequested(std::uint64_t body, const QString& item);
    void suppressRequested(std::uint64_t body, const QString& item, bool suppressed);
    void deleteRequested(std::uint64_t body, const QString& item);

private:
    void menuAt(const QPoint& pos);
    QListWidget* list_{nullptr};
    QLabel* title_{nullptr};
    std::uint64_t body_{0};
    bool updating_{false};
};

}  // namespace orv
