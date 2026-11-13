// SPDX-License-Identifier: GPL-3.0-or-later
// openrevers-cli — OpenRevers without a window.
//   openrevers-cli mcp [--headless] [--lang es|en]   MCP server on stdio for an AI agent
//   openrevers-cli list                               the commands, as JSON
//   openrevers-cli run <command> [json]               run commands in sequence on one document
//   openrevers-cli --version | --help
#include "commands/Command.h"
#include "core/util/Config.h"
#include "core/util/Version.h"
#include "mcp/AgentLink.h"
#include "mcp/LocalBackend.h"
#include "mcp/McpServer.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace orv;

namespace {

/// Keep standard output for our own replies. Some libraries (QuadriFlow)
/// print progress with printf; on stdout that would break the MCP stream or
/// the JSON of `run`. The real stdout gets a private handle, and the process'
/// stdout is pointed at stderr, where the progress is harmless.
FILE* reserveStdout() {
    std::fflush(stdout);
#ifdef _WIN32
    const int fd = _dup(_fileno(stdout));
    if (fd < 0) return stdout;
    _dup2(_fileno(stderr), _fileno(stdout));
    FILE* f = _fdopen(fd, "wb");
#else
    const int fd = dup(fileno(stdout));
    if (fd < 0) return stdout;
    dup2(fileno(stderr), fileno(stdout));
    FILE* f = fdopen(fd, "w");
#endif
    return f ? f : stdout;
}

void emit(FILE* out, const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), out);
    std::fputc('\n', out);
    std::fflush(out);
}

}  // namespace

namespace {

const char* kHelp = R"(openrevers-cli — OpenRevers without a window

Usage:
  openrevers-cli mcp [--headless] [--lang es|en]
      MCP server on stdio, for Claude Desktop, Claude Code and other agents.
      Connects to the running OpenRevers app (the person sees every step);
      with no app running, or with --headless, works on a document of its own.
  openrevers-cli list
      Every command with its input schema, as JSON.
  openrevers-cli run <command> [<json>] [<command> [<json>]]...
      Run commands one after another on one document, e.g.
      openrevers-cli run mesh.import '{"path":"part.stl"}' mesh.repair '{"body":"b1"}' mesh.export '{"body":"b1","path":"clean.stl"}'
  openrevers-cli --version

OpenRevers makes no connection to the internet; the MCP server talks only to
the agent that started it and to the app on 127.0.0.1.
)";

std::string systemLang() {
    for (const char* var : {"LANG", "LC_ALL", "LC_MESSAGES"}) {
#ifdef _MSC_VER
        char* v = nullptr;
        size_t n = 0;
        if (_dupenv_s(&v, &n, var) == 0 && v) {
            std::string s(v);
            free(v);
            if (s.rfind("es", 0) == 0) return "es";
        }
#else
        if (const char* v = std::getenv(var); v && std::string(v).rfind("es", 0) == 0) return "es";
#endif
    }
    return "en";
}

int runMcp(const std::vector<std::string>& args) {
    bool headless = false;
    std::string lang = systemLang();
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--headless") headless = true;
        else if (args[i] == "--lang" && i + 1 < args.size()) lang = args[++i];
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    FILE* const out = reserveStdout();
    const AgentSettings settings = loadAgentSettings();
    CommandRegistry registry;
    registerCoreCommands(registry, loadAgentSettings().experimentalScan);
    Document doc;
    std::unique_ptr<ToolBackend> backend;
    if (!headless && settings.enabled) backend = AgentLinkClient::connect(settings.port, settings.token, lang);
    if (backend) {
        std::cerr << "[openrevers-cli] Connected to the OpenRevers app on 127.0.0.1:" << settings.port << ".\n";
    } else {
        std::cerr << "[openrevers-cli] No OpenRevers app found on 127.0.0.1:" << settings.port << "; working headless.\n";
        backend = std::make_unique<LocalBackend>(registry, doc, PathPolicy::forAgent(settings), lang, "headless", false);
    }
    McpServer server(*backend, lang);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (auto reply = server.handleLine(line)) emit(out, *reply);
    }
    return 0;
}

int runCommands(const std::vector<std::string>& args) {
    CommandRegistry registry;
    registerCoreCommands(registry, loadAgentSettings().experimentalScan);
    Document doc;
    CommandContext ctx{doc, PathPolicy::unrestricted(), systemLang(), "script", nullptr};
    FILE* const out = reserveStdout();
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string name = args[i];
        json params = json::object();
        if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] == '{') {
            try {
                params = json::parse(args[++i]);
            } catch (const json::exception& e) {
                std::cerr << name << ": the input is not JSON: " << e.what() << '\n';
                return 2;
            }
        }
        try {
            const json r = registry.run(name, params, ctx);
            emit(out, json{{"command", name}, {"result", r}}.dump(2));
        } catch (const CommandError& e) {
            std::cerr << name << ": " << e.code() << ": " << e.what() << '\n';
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout << kHelp;
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version") {
        std::cout << kAppName << ' ' << kVersionString << '\n';
        return 0;
    }
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (args[0] == "mcp") return runMcp(rest);
    if (args[0] == "run") return runCommands(rest);
    if (args[0] == "list") {
        CommandRegistry registry;
        registerCoreCommands(registry, loadAgentSettings().experimentalScan);
        std::cout << describeCommands(registry, false).dump(2) << '\n';
        return 0;
    }
    std::cerr << "Unknown command \"" << args[0] << "\".\n\n" << kHelp;
    return 2;
}
