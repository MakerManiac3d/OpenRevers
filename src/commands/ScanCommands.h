// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "commands/Command.h"

namespace orv {
/// scan.*: scanning straight into the project (phase 6).
void registerScanCommands(CommandRegistry& registry);
}  // namespace orv
