// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp/LocalBackend.h"

namespace orv {

json describeCommands(const CommandRegistry& registry, bool includeAppOnly) {
    json list = json::array();
    for (const Command* c : registry.all()) {
        if (c->appOnly && !includeAppOnly) continue;
        list.push_back({{"name", c->name},
                        {"group", c->group},
                        {"title", {{"en", c->title.en}, {"es", c->title.es}}},
                        {"description", {{"en", c->description.en}, {"es", c->description.es}}},
                        {"schema", c->schema},
                        {"mutates", c->mutates},
                        {"appOnly", c->appOnly}});
    }
    return list;
}

LocalBackend::LocalBackend(const CommandRegistry& registry, Document& doc, PathPolicy paths, std::string lang, std::string mode, bool appCommands)
    : registry_(registry), doc_(doc), paths_(std::move(paths)), lang_(std::move(lang)), mode_(std::move(mode)), appCommands_(appCommands) {}

json LocalBackend::listCommands() { return describeCommands(registry_, appCommands_); }

json LocalBackend::call(const std::string& name, const json& params) {
    const Command* c = registry_.find(name);
    if (!c) throw CommandError("not_found", "No tool \"" + name + "\"");
    if (c->appOnly && !appCommands_)
        throw CommandError("unavailable", name + " needs the OpenRevers desktop app, which is not connected: open it and restart this MCP server.");
    std::lock_guard lock(runMutex_);
    CommandContext ctx{doc_, paths_, lang_, "agent", nullptr};
    return registry_.run(c->name, params, ctx);
}

}  // namespace orv
