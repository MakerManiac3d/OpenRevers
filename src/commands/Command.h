// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — commands: every action of the interface, of a script and of an
// AI agent is a command with a JSON input (described by a JSON Schema) and a
// JSON result. A command that changes the document is one undo step.
#pragma once

#include "core/util/Config.h"
#include "doc/Document.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace orv {

using nlohmann::json;

/// A text in the two languages of the interface.
struct Text {
    std::string en, es;
    const std::string& in(const std::string& lang) const { return lang == "es" && !es.empty() ? es : en; }
};

/// A command failure the caller can act on. `code` is one of
/// invalid_input, not_found, refused, failed, unavailable.
class CommandError : public std::runtime_error {
public:
    CommandError(std::string code, const std::string& message) : std::runtime_error(message), code_(std::move(code)) {}
    const std::string& code() const { return code_; }

private:
    std::string code_;
};

/// What a command runs with.
struct CommandContext {
    Document& doc;
    PathPolicy paths = PathPolicy::unrestricted();
    std::string lang = "en";
    std::string caller = "user";  // user, script or agent
    std::function<void(double fraction, const std::string& what)> progress;

    /// A sentence in the caller's language.
    std::string say(const std::string& en, const std::string& es) const { return lang == "es" ? es : en; }
};

struct Command {
    std::string name;          // "mesh.fill_holes"; agents see "mesh_fill_holes"
    std::string group;         // menu / ribbon group: file, mesh, body, view, history, help
    Text title;
    Text description;
    json schema;               // JSON Schema of the input object
    bool mutates{false};       // one undo step
    bool appOnly{false};       // needs the desktop interface (views, pictures)
    std::function<json(CommandContext&, const json&)> run;
};

class CommandRegistry {
public:
    void add(Command c);
    const Command* find(const std::string& name) const;
    std::vector<const Command*> all() const;

    /// Validate `params` against the command's schema (filling defaults), run
    /// it, and make it one undo step. Throws CommandError.
    json run(const std::string& name, const json& params, CommandContext& ctx) const;

    /// The input with defaults filled; throws CommandError(invalid_input).
    static json validate(const Command& c, const json& params);

private:
    std::map<std::string, std::unique_ptr<Command>> commands_;
    std::vector<std::string> order_;
};

/// Builders for the parts of a schema the commands share.
namespace schema {
json object(std::initializer_list<std::pair<const char*, json>> props, std::vector<std::string> required = {});
json body(const Text& description);
json string(const Text& description, const char* format = nullptr);
json path(const Text& description, bool save, const char* filter);
json number(const Text& description, double def, double min, double max, const char* unit = nullptr);
json integer(const Text& description, long long def, long long min, long long max);
json boolean(const Text& description, bool def);
json choice(const Text& description, std::vector<std::string> values, const std::string& def);
/// A point or vector: three numbers [x, y, z].
json point(const Text& description, const char* unit = "mm");
/// The same schema marked with the control the interface gives it (FieldKind);
/// `of` names the field holding the body it belongs to (default "body").
json kind(json s, const char* k, const char* of = nullptr);
}  // namespace schema

/// The commands that need no interface: files, mesh tools, bodies, history, help.
void registerCoreCommands(CommandRegistry& registry);

}  // namespace orv
