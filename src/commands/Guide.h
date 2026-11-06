// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace orv {
/// The working guide served to agents (guide.read) and shown in Help.
/// topic: all, workflow, mesh, safety. lang: en or es.
std::string guideText(const std::string& lang, const std::string& topic);
}  // namespace orv
