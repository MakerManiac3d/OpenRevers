// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commands/Command.h"
#include "doc/Features.h"

namespace orv {

void registerFeatureCommands(CommandRegistry& registry);
json featureJson(const Feature& f);
json measurePair(const Primitive& a, const Primitive& b);

}  // namespace orv
