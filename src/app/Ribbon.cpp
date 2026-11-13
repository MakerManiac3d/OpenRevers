// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Ribbon.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>

namespace orv {

QString ribbonLabel(const QString& text) {
    QString t = text;
    t.remove('&');
    if (t.endsWith(QStringLiteral("…"))) t.chop(1);
    if (t.endsWith("...")) t.chop(3);
    t = t.trimmed();
    if (t.size() <= 13) return t;
    // Greedy lines of about 14 characters, at most three.
    const QStringList words = t.split(' ', Qt::SkipEmptyParts);
    QStringList lines;
    QString cur;
    for (const QString& w : words) {
        if (!cur.isEmpty() && cur.size() + 1 + w.size() > 14 && lines.size() < 2) {
            lines << cur;
            cur = w;
        } else {
            cur = cur.isEmpty() ? w : cur + ' ' + w;
        }
    }
    if (!cur.isEmpty()) lines << cur;
    return lines.join('\n');
}

Ribbon::Ribbon(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    tabs_ = new QTabBar();
    tabs_->setDocumentMode(true);
    tabs_->setExpanding(false);
    tabs_->setDrawBase(true);
    pages_ = new QStackedWidget();
    v->addWidget(tabs_);
    v->addWidget(pages_);
    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int Ribbon::addTab(const QString& title) {
    // A tab wider than the window scrolls sideways rather than squeezing
    // its buttons (which would cut their labels).
    auto* scroll = new QScrollArea();
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* page = new QWidget();
    scroll->setWidget(page);
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(6, 4, 6, 2);
    row->setSpacing(2);
    row->addStretch(1);
    rows_.append(row);
    pages_->addWidget(scroll);
    return tabs_->addTab(title);
}

void Ribbon::addGroup(int tab, const QString& title, const QList<QAction*>& actions) {
    if (tab < 0 || tab >= rows_.size()) return;
    auto* row = rows_[tab];
    auto* group = new QWidget();
    auto* gv = new QVBoxLayout(group);
    gv->setContentsMargins(2, 0, 2, 0);
    gv->setSpacing(0);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(0);
    for (QAction* a : actions) {
        if (!a) continue;
        // The button mirrors the action but keeps its own (wrapped) label:
        // a default action would put the menu text back on every change.
        auto* b = new QToolButton();
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(30, 30));
        b->setAutoRaise(true);
        b->setIcon(a->icon());
        b->setText(ribbonLabel(a->text()));
        // As wide as the longest line of its label (Qt's own hint can be
        // too narrow for a two-line label, and then cuts it).
        int widest = 0;
        for (const QString& line : b->text().split('\n')) widest = std::max(widest, b->fontMetrics().horizontalAdvance(line));
        b->setFixedWidth(std::max(56, widest + 10));
        b->setCheckable(a->isCheckable());
        b->setChecked(a->isChecked());
        b->setEnabled(a->isEnabled());
        const QString name = ribbonLabel(a->text()).replace('\n', ' ');
        const QString shortcut = a->shortcut().isEmpty() ? QString() : " (" + a->shortcut().toString(QKeySequence::NativeText) + ")";
        b->setToolTip(a->toolTip().isEmpty() || ribbonLabel(a->toolTip()) == ribbonLabel(a->text()) ? name + shortcut
                                                                                                    : "<b>" + name + shortcut + "</b><br>" + a->toolTip());
        connect(b, &QToolButton::clicked, a, [a]() { a->trigger(); });
        connect(a, &QAction::changed, b, [a, b]() {
            b->setEnabled(a->isEnabled());
            b->setChecked(a->isChecked());
            b->setIcon(a->icon());
        });
        connect(a, &QAction::toggled, b, &QToolButton::setChecked);
        buttons->addWidget(b, 0, Qt::AlignTop);
    }
    gv->addLayout(buttons);
    auto* label = new QLabel(title);
    label->setAlignment(Qt::AlignCenter);
    QFont f = label->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    label->setFont(f);
    label->setEnabled(false);  // the style's muted colour
    gv->addWidget(label);
    // Groups go before the stretch, with a thin line between them.
    const int at = row->count() - 1;
    if (at > 0) {
        auto* line = new QFrame();
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        row->insertWidget(at, line);
        row->insertWidget(at + 1, group);
    } else {
        row->insertWidget(at, group);
    }
    // Every tab as tall as its tallest group, plus room for a scroll bar.
    int h = 0;
    for (auto* r : rows_) h = std::max(h, r->parentWidget()->sizeHint().height());
    pages_->setFixedHeight(h + 14);
}

int Ribbon::currentTab() const { return tabs_->currentIndex(); }
void Ribbon::setCurrentTab(int tab) { tabs_->setCurrentIndex(tab); }
QString Ribbon::tabTitle(int tab) const { return tabs_->tabText(tab); }

}  // namespace orv
