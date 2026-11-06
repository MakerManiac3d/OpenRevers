// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the link between the MCP bridge (openrevers-cli mcp) and the
// desktop app: one TCP connection on 127.0.0.1, one JSON object per line.
//   request : {"token": "...", "id": 1, "op": "list" | "call", "name": "...", "params": {...}, "lang": "es"}
//   response: {"id": 1, "ok": true, "result": ...} | {"id": 1, "ok": false, "code": "...", "message": "..."}
// The token in agent.json (configDir()) proves the bridge runs as the same user.
#pragma once

#include "mcp/McpServer.h"

#include <memory>
#include <string>

namespace orv {

class AgentLinkClient : public ToolBackend {
public:
    /// Connects to the app at 127.0.0.1:port; returns nullptr when nothing listens there.
    static std::unique_ptr<AgentLinkClient> connect(int port, std::string token, std::string lang);
    ~AgentLinkClient() override;

    json listCommands() override;
    json call(const std::string& name, const json& params) override;
    std::string mode() const override { return "app"; }

private:
    AgentLinkClient() = default;
    json request(json req);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string token_, lang_;
    long long nextId_{1};
};

}  // namespace orv
