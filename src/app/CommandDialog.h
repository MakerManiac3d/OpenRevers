// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the form of a command, built from its JSON Schema: the same
// inputs a person fills in here are the ones an agent sends.
#pragma once

#include <array>
#include <optional>

#include "commands/Command.h"

#include <QDialog>

#include <functional>
#include <map>

class QFormLayout;

namespace orv {

class CommandDialog : public QDialog {
    Q_OBJECT
public:
    /// `selectedFeatures` prefill the shape fields in order (the shapes picked
    /// in the view or the Shapes panel, first picked first); `pickedPoint`
    /// prefills the point fields "at" and "about".
    CommandDialog(const Command& command, const std::vector<Body>& bodies, const QString& selectedBody, const QString& lang, QWidget* parent = nullptr,
                  const QStringList& selectedFeatures = {}, const std::optional<std::array<double, 3>>& pickedPoint = std::nullopt);
    json values() const;

private:
    const Command& command_;
    std::map<std::string, std::function<json()>> readers_;
};

}  // namespace orv
