// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the form of a command, built from its JSON Schema: the same
// inputs a person fills in here are the ones an agent sends.
#pragma once

#include "commands/Command.h"

#include <QDialog>

#include <functional>
#include <map>

class QFormLayout;

namespace orv {

class CommandDialog : public QDialog {
    Q_OBJECT
public:
    /// `selectedFeature` prefills the first shape field (the shape picked in the Shapes panel).
    CommandDialog(const Command& command, const std::vector<Body>& bodies, const QString& selectedBody, const QString& lang, QWidget* parent = nullptr,
                  const QString& selectedFeature = {});
    json values() const;

private:
    const Command& command_;
    std::map<std::string, std::function<json()>> readers_;
};

}  // namespace orv
