// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the Model Context Protocol server (JSON-RPC 2.0), transport
// independent: it takes one message and gives back the reply, if any.
// Spec: https://modelcontextprotocol.io/specification (revision 2025-06-18).
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace orv {

using nlohmann::json;

/// Where the tools run: in this process (headless) or in the desktop app.
class ToolBackend {
public:
    virtual ~ToolBackend() = default;
    /// [{name, group, title{en,es}, description{en,es}, schema, mutates, appOnly}]
    virtual json listCommands() = 0;
    /// The command's result; throws CommandError.
    virtual json call(const std::string& name, const json& params) = 0;
    /// "app" or "headless", for the agent to know.
    virtual std::string mode() const = 0;
};

class McpServer {
public:
    McpServer(ToolBackend& backend, std::string lang);
    /// Handle one JSON-RPC message; returns the response (nothing for a notification).
    std::optional<json> handle(const json& message);
    /// Handle one line of text (stdio transport): parse errors become JSON-RPC errors.
    std::optional<std::string> handleLine(const std::string& line);

    static std::string toolName(const std::string& command);  // mesh.fill_holes → mesh_fill_holes

private:
    json initialize(const json& params);
    json listTools();
    json callTool(const json& params);

    ToolBackend& backend_;
    std::string lang_;
};

}  // namespace orv
