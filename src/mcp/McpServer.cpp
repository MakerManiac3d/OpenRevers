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

namespace {
/// A command that changes the model can be tried first: dryRun works it out on
/// a copy and says what it would change, touching nothing.
json withDryRun(json schema, bool mutates, bool es) {
    if (!mutates || !schema.is_object()) return schema;
    schema["properties"]["dryRun"] = {{"type", "boolean"},
                                      {"description", es ? "Probar sin cambiar nada: devuelve el resultado y qué cuerpos cambiarían (preview)"
                                                         : "Try it without changing anything: returns the result and which bodies would change (preview)"}};
    return schema;
}
}  // namespace

json McpServer::listTools() {
    json tools = json::array();
    const bool es = lang_ == "es";
    for (const auto& c : backend_.listCommands()) {
        const std::string title = es ? c["title"].value("es", c["title"].value("en", "")) : c["title"].value("en", "");
        const std::string desc = es ? c["description"].value("es", c["description"].value("en", "")) : c["description"].value("en", "");
        json t = {{"name", toolName(c["name"].get<std::string>())},
                  {"title", title},
                  {"description", desc},
                  {"inputSchema", withDryRun(cleanSchema(c["schema"]), c.value("mutates", false), es)},
                  {"annotations", {{"readOnlyHint", !c.value("mutates", false) && c["name"] != "mesh.export" && c["name"] != "project.save"},
                                   {"destructiveHint", c["name"] == "body.delete"},
                                   {"openWorldHint", false}}}};
        tools.push_back(t);
    }
    // Two fixed tools that reach every command, also those added after the
    // client cached this list (some clients only read it once per session).
    tools.push_back({{"name", "command_list"},
                     {"title", es ? "Comandos disponibles" : "Available commands"},
                     {"description", es ? "Todos los comandos de esta versión de OpenRevers (nombre, título y si cambian el documento). Con 'command', su esquema completo. Úsalo si una herramienta que esperas no aparece en tu lista: puede que tu cliente guarde una lista antigua; llámala con command_run."
                                         : "Every command of this OpenRevers version (name, title, whether it changes the document). With 'command', its full schema. Use it when a tool you expect is missing from your list: your client may keep an old list; call it with command_run."},
                     {"inputSchema", {{"type", "object"}, {"properties", {{"command", {{"type", "string"}, {"description", "e.g. scan.status"}}}}}}},
                     {"annotations", {{"readOnlyHint", true}, {"openWorldHint", false}}}});
    tools.push_back({{"name", "command_run"},
                     {"title", es ? "Ejecutar un comando" : "Run a command"},
                     {"description", es ? "Ejecuta cualquier comando de OpenRevers por su nombre (scan.status o scan_status) con sus argumentos, igual que su propia herramienta. Admite dryRun en los que cambian el documento."
                                         : "Runs any OpenRevers command by name (scan.status or scan_status) with its arguments, just like its own tool. Commands that change the document take dryRun."},
                     {"inputSchema", {{"type", "object"},
                                      {"properties", {{"command", {{"type", "string"}}}, {"arguments", {{"type", "object"}}}}},
                                      {"required", {"command"}}}},
                     {"annotations", {{"readOnlyHint", false}, {"destructiveHint", true}, {"openWorldHint", false}}}});
    return {{"tools", tools}};
}

json McpServer::callTool(const json& params) {
    const std::string name = params.value("name", "");
    const json args = params.value("arguments", json::object());
    try {
        json result;
        if (name == "command_list") {
            const std::string want = args.value("command", "");
            const bool es = lang_ == "es";
            json list = json::array();
            for (const auto& c : backend_.listCommands()) {
                const std::string n = c["name"].get<std::string>();
                if (!want.empty()) {
                    if (n != want && toolName(n) != want) continue;
                    result = c;
                    result["schema"] = withDryRun(cleanSchema(c["schema"]), c.value("mutates", false), es);
                    break;
                }
                list.push_back({{"name", n},
                                {"title", es ? c["title"].value("es", c["title"].value("en", "")) : c["title"].value("en", "")},
                                {"mutates", c.value("mutates", false)}});
            }
            if (!want.empty() && result.is_null()) throw CommandError("not_found", "No command \"" + want + "\"");
            if (want.empty()) result = {{"commands", list}};
        } else if (name == "command_run") {
            const std::string cmd = args.value("command", "");
            std::string dotted = cmd;
            for (const auto& c : backend_.listCommands())
                if (toolName(c["name"].get<std::string>()) == cmd) dotted = c["name"].get<std::string>();
            result = backend_.call(dotted, args.value("arguments", json::object()));
        } else {
            result = backend_.call(name, args);
        }
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
