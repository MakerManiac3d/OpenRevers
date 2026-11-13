// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the commands only the desktop app has: the view and pictures of it.
#pragma once

#include "commands/Command.h"

#include <functional>

namespace orv {
class Viewport;

/// What the person has picked in the view, and picking on their behalf.
struct SelectionAccess {
    std::function<json()> selection;
    std::function<json(double x, double y, bool add)> pick;  // x, y: 0..1 across the view
    std::function<json(const std::string& name)> save;     // keeps the selection under a name
    std::function<json(const std::string& name)> saved;    // a kept selection (throws if none)
};

void registerAppCommands(CommandRegistry& registry, Viewport& viewport, SelectionAccess selection);
}  // namespace orv
