// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the form of a command, docked beside the view and not modal:
// built from the command's schema, each field with a control of its kind
// (FieldKind), and the ones that name a place taken by clicking in the view.
// The JSON it sends is the JSON an agent sends.
#pragma once

#include "app/Viewport.h"
#include "commands/Command.h"
#include "commands/FieldKind.h"

#include <QWidget>

#include <functional>
#include <vector>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QTimer;
class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace orv {

class CommandPanel : public QWidget {
    Q_OBJECT
public:
    struct Context {
        std::vector<Body> bodies;
        QString selectedBody;       // "b3"
        QString lang{"en"};
        QStringList pickedShapes;   // shape refs picked, first first
        Pick lastPick;
    };
    explicit CommandPanel(QWidget* parent = nullptr);

    /// Show `command`'s form, prefilled from `context` and then from `initial`.
    void open(const Command& command, const Context& context, const json& initial = json::object());
    /// The document changed: rebuild the lists, keeping what was filled in.
    void refresh(const Context& context);
    const Command* command() const { return command_; }
    json values() const;

    /// Whether a field is waiting for a click in the view, and that click.
    bool wantsPick() const;
    void takePick(const Pick& pick);
    /// The points of the form as handles to drag in the view, and a dragged one back.
    std::vector<Viewport::Handle> handles() const;
    void moveHandle(const QString& id, const Vec3d& pos);
    /// "See it live": the result shown as it would be, while the form changes.
    bool live() const;
    /// Where the lasso selection is read from (mesh-faces fields).
    void setSelectionSource(std::function<std::pair<std::uint64_t, std::vector<std::uint32_t>>()> source) { selection_ = std::move(source); }

signals:
    void applyRequested(const QString& name, const json& params);
    /// The loops of a sketch are drawn in the sketch editor: `values` = what the form holds.
    void sketchEditorRequested(const QString& command, const json& values);
    void closeRequested();
    /// Something in the form changed (checked a few times a second).
    void valuesChanged();

private:
    struct Field {
        std::string key;
        FieldKind kind{FieldKind::Text};
        std::function<json()> read;
        std::function<void(const json&)> write;
        std::function<void(const Pick&)> pick;  // empty: not filled from the view
        bool keepArmed{false};                  // a list of clicks: stays armed
        QAbstractButton* arm{nullptr};
        bool handle{false};  // a position to drag in the view
    };
    void build(const json& initial);
    QWidget* makeField(Field& f, const json& s, bool required);
    QString bodyOf(const std::string& ofKey) const;
    const Body* findBody(const QString& ref) const;

    const Command* command_{nullptr};
    Context ctx_;
    std::vector<Field> fields_;
    std::map<std::string, QComboBox*> bodyBoxes_;            // body fields, by key
    std::vector<std::function<void()>> dependents_;         // refill when a body changes
    std::function<std::pair<std::uint64_t, std::vector<std::uint32_t>>()> selection_;
    QVBoxLayout* layout_{nullptr};
    QLabel* title_{nullptr};
    QLabel* intro_{nullptr};
    QWidget* form_{nullptr};
    QButtonGroup* arms_{nullptr};
    QPushButton* apply_{nullptr};
    QCheckBox* live_{nullptr};
    QTimer* poll_{nullptr};
    json last_;
};

}  // namespace orv
