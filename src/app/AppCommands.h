// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the commands only the desktop app has: the view and pictures of it.
#pragma once

#include "commands/Command.h"

namespace orv {
class Viewport;
void registerAppCommands(CommandRegistry& registry, Viewport& viewport);
}  // namespace orv
