// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the ribbon: tabs by task (Solid, Sketch, Surface, Mesh...),
// each a row of labelled groups of tool buttons, as in Fusion. The buttons
// are the menus' own actions, so a tool does the same wherever it is used.
#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QStackedWidget;
class QTabBar;
class QHBoxLayout;

namespace orv {

class Ribbon : public QWidget {
    Q_OBJECT
public:
    explicit Ribbon(QWidget* parent = nullptr);

    /// Adds a tab and returns its index.
    int addTab(const QString& title);
    /// Adds a labelled group of buttons to a tab; null actions are skipped.
    void addGroup(int tab, const QString& title, const QList<QAction*>& actions);
    int currentTab() const;
    void setCurrentTab(int tab);
    QString tabTitle(int tab) const;

private:
    QTabBar* tabs_{nullptr};
    QStackedWidget* pages_{nullptr};
    QList<QHBoxLayout*> rows_;
};

/// Breaks a button label into two balanced lines when it is long.
QString ribbonLabel(const QString& text);

}  // namespace orv
