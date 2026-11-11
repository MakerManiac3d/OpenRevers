// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace orv {
class CommandRegistry;
/// part.* (parametric parts, STEP, comparing with the scan) and mesh.section.
void registerPartCommands(CommandRegistry& registry);
}  // namespace orv
