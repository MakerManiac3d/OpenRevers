// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commands/Command.h"

namespace orv {

/// Measuring a scan as it is: rays through it, its radial profile and its
/// unwrapped map round an axis.
void registerProbeCommands(CommandRegistry& registry);

}  // namespace orv
