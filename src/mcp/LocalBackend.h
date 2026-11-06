// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — tools run in this process, on a document of its own (headless
// mode of the bridge, scripts, tests) or on the desktop app's document.
#pragma once

#include "commands/Command.h"
#include "mcp/McpServer.h"

#include <functional>
#include <mutex>

namespace orv {

/// Commands described for a list: the shape ToolBackend::listCommands returns.
json describeCommands(const CommandRegistry& registry, bool includeAppOnly);

class LocalBackend : public ToolBackend {
public:
    LocalBackend(const CommandRegistry& registry, Document& doc, PathPolicy paths, std::string lang, std::string mode, bool appCommands);
    json listCommands() override;
    json call(const std::string& name, const json& params) override;
    std::string mode() const override { return mode_; }

private:
    const CommandRegistry& registry_;
    Document& doc_;
    PathPolicy paths_;
    std::string lang_, mode_;
    bool appCommands_;
    std::mutex runMutex_;  // one command at a time
};

}  // namespace orv
