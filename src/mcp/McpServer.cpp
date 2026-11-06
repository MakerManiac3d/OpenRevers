// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp/McpServer.h"

#include "commands/Command.h"
#include "core/util/Version.h"

#include <algorithm>
#include <array>

namespace orv {

namespace {
constexpr std::array<const char*, 3> kProtocols{"2025-06-18", "2025-03-26", "2024-11-05"};

json rpcError(const json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

/// JSON Schema as clients expect it: our "x-" annotations stripped.
json cleanSchema(json s) {
    if (s.is_object()) {
        for (auto it = s.begin(); it != s.end();) {
            if (it.key().rfind("x-", 0) == 0) it = s.erase(it);
            else {
                *it = cleanSchema(*it);
                ++it;
            }
        }
    } else if (s.is_array()) {
        for (auto& e : s) e = cleanSchema(e);
    }
    return s;
}
}  // namespace

McpServer::McpServer(ToolBackend& backend, std::string lang) : backend_(backend), lang_(std::move(lang)) {}

std::string McpServer::toolName(const std::string& command) {
    std::string n = command;
    std::replace(n.begin(), n.end(), '.', '_');
    return n;
}

std::optional<std::string> McpServer::handleLine(const std::string& line) {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) return std::nullopt;
    json msg;
    try {
        msg = json::parse(line);
    } catch (const json::exception& e) {
        return rpcError(nullptr, -32700, std::string("Parse error: ") + e.what()).dump();
    }
    if (msg.is_array()) {  // batch (older protocol revisions)
        json out = json::array();
        for (const auto& m : msg)
            if (auto r = handle(m)) out.push_back(*r);
        if (out.empty()) return std::nullopt;
        return out.dump();
    }
    auto r = handle(msg);
    if (!r) return std::nullopt;
    return r->dump();
}

std::optional<json> McpServer::handle(const json& msg) {
    if (!msg.is_object() || msg.value("jsonrpc", "") != "2.0") return rpcError(msg.is_object() ? msg.value("id", json()) : json(), -32600, "Invalid request");
    const bool isRequest = msg.contains("id") && msg.contains("method");
    if (!msg.contains("method")) return std::nullopt;  // a response to us: we send no requests
    const std::string method = msg["method"].get<std::string>();
    const json params = msg.value("params", json::object());
    if (!isRequest) return std::nullopt;  // notifications/initialized, cancelled…
    const json id = msg["id"];
    try {
        json result;
        if (method == "initialize") result = initialize(params);
        else if (method == "ping") result = json::object();
        else if (method == "tools/list") result = listTools();
        else if (method == "tools/call") result = callTool(params);
        else if (method == "resources/list") result = {{"resources", json::array()}};
        else if (method == "prompts/list") result = {{"prompts", json::array()}};
        else return rpcError(id, -32601, "Method not found: " + method);
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    } catch (const std::exception& e) {
        return rpcError(id, -32603, e.what());
    }
}

json McpServer::initialize(const json& params) {
    const std::string asked = params.value("protocolVersion", std::string(kProtocols[0]));
    std::string version = kProtocols[0];
    for (auto p : kProtocols)
        if (asked == p) version = p;
    const bool es = lang_ == "es";
    std::string instructions = es
        ? "Estas herramientas manejan OpenRevers, un programa de ingeniería inversa de mallas escaneadas que funciona en local. "
          "Empieza con session_state y lee guide_read antes de la primera tarea. Cada cambio es un paso de deshacer (history_undo). "
          "Unidades: milímetros. Modo actual: "
        : "These tools drive OpenRevers, a local reverse-engineering program for scanned meshes. "
          "Start with session_state and read guide_read before the first task. Every change is one undo step (history_undo). "
          "Units: millimetres. Current mode: ";
    instructions += backend_.mode() == "app"
        ? (es ? "conectado a la aplicación de escritorio: la persona ve cada paso." : "connected to the desktop app: the person sees every step.")
        : (es ? "sin interfaz (headless): no hay visor; abre la aplicación de escritorio y reinicia el servidor para trabajar a la vista."
              : "headless: there is no viewer; open the desktop app and restart this server to work in view.");
    return {{"protocolVersion", version},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "openrevers"}, {"title", "OpenRevers"}, {"version", kVersionString}}},
            {"instructions", instructions}};
}

json McpServer::listTools() {
    json tools = json::array();
    const bool es = lang_ == "es";
    for (const auto& c : backend_.listCommands()) {
        const std::string title = es ? c["title"].value("es", c["title"].value("en", "")) : c["title"].value("en", "");
        const std::string desc = es ? c["description"].value("es", c["description"].value("en", "")) : c["description"].value("en", "");
        json t = {{"name", toolName(c["name"].get<std::string>())},
                  {"title", title},
                  {"description", desc},
                  {"inputSchema", cleanSchema(c["schema"])},
                  {"annotations", {{"readOnlyHint", !c.value("mutates", false) && c["name"] != "mesh.export" && c["name"] != "project.save"},
                                   {"destructiveHint", c["name"] == "body.delete"},
                                   {"openWorldHint", false}}}};
        tools.push_back(t);
    }
    return {{"tools", tools}};
}

json McpServer::callTool(const json& params) {
    const std::string name = params.value("name", "");
    const json args = params.value("arguments", json::object());
    try {
        json result = backend_.call(name, args);
        json content = json::array();
        if (result.is_object() && result.contains("image") && result["image"].is_string()) {
            content.push_back({{"type", "image"}, {"data", result["image"]}, {"mimeType", result.value("mimeType", "image/png")}});
            result.erase("image");
        }
        content.push_back({{"type", "text"}, {"text", result.dump()}});
        json out = {{"content", content}, {"isError", false}};
        if (result.is_object()) out["structuredContent"] = result;
        return out;
    } catch (const CommandError& e) {
        return {{"content", {{{"type", "text"}, {"text", e.code() + ": " + e.what()}}}}, {"isError", true}};
    } catch (const std::exception& e) {
        return {{"content", {{{"type", "text"}, {"text", std::string("failed: ") + e.what()}}}}, {"isError", true}};
    }
}

}  // namespace orv
