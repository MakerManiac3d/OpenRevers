// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/Command.h"

#include <cmath>

namespace orv {

void CommandRegistry::add(Command c) {
    const std::string name = c.name;
    if (!commands_.count(name)) order_.push_back(name);
    commands_[name] = std::make_unique<Command>(std::move(c));
}

const Command* CommandRegistry::find(const std::string& name) const {
    auto it = commands_.find(name);
    if (it != commands_.end()) return it->second.get();
    // Agents call "mesh_fill_holes" for "mesh.fill_holes".
    for (const auto& [n, c] : commands_) {
        std::string alt = n;
        for (auto& ch : alt)
            if (ch == '.') ch = '_';
        if (alt == name) return c.get();
    }
    return nullptr;
}

std::vector<const Command*> CommandRegistry::all() const {
    std::vector<const Command*> out;
    for (const auto& n : order_) out.push_back(commands_.at(n).get());
    return out;
}

json CommandRegistry::validate(const Command& c, const json& in) {
    const json params = in.is_null() ? json::object() : in;
    if (!params.is_object()) throw CommandError("invalid_input", c.name + ": the input must be a JSON object");
    const json& props = c.schema.value("properties", json::object());
    json out = json::object();
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!props.contains(it.key())) {
            std::string known;
            for (auto p = props.begin(); p != props.end(); ++p) known += (known.empty() ? "" : ", ") + p.key();
            throw CommandError("invalid_input", c.name + ": unknown input \"" + it.key() + "\" (expected: " + (known.empty() ? "none" : known) + ")");
        }
    }
    for (auto p = props.begin(); p != props.end(); ++p) {
        const std::string& key = p.key();
        const json& s = p.value();
        if (!params.contains(key) || params[key].is_null()) {
            if (s.contains("default")) out[key] = s["default"];
            continue;
        }
        json v = params[key];
        std::string type = s.contains("type") && s["type"].is_string() ? s["type"].get<std::string>() : "";
        if (s.contains("type") && s["type"].is_array()) {  // a choice of types: number or a parameter's name
            for (const auto& t : s["type"]) {
                const std::string tt = t.get<std::string>();
                if ((tt == "number" && v.is_number()) || (tt == "string" && v.is_string())) type = tt;
            }
            if (type.empty()) throw CommandError("invalid_input", c.name + ": input \"" + key + "\" must be " + s["type"].dump());
        }
        auto bad = [&](const std::string& why) { throw CommandError("invalid_input", c.name + ": input \"" + key + "\" " + why); };
        if ((type == "number" || type == "integer") && v.is_string()) {  // some clients send numbers as text
            try {
                std::size_t used = 0;
                const double d = std::stod(v.get<std::string>(), &used);
                if (used == v.get<std::string>().size()) v = d;
            } catch (...) {
            }
        }
        if (type == "number") {
            if (!v.is_number()) bad("must be a number");
            const double d = v.get<double>();
            if (!std::isfinite(d)) bad("must be finite");
            if (s.contains("minimum") && d < s["minimum"].get<double>()) bad("must be at least " + s["minimum"].dump());
            if (s.contains("maximum") && d > s["maximum"].get<double>()) bad("must be at most " + s["maximum"].dump());
        } else if (type == "integer") {
            if (v.is_number_float() && std::floor(v.get<double>()) == v.get<double>()) v = static_cast<long long>(v.get<double>());
            if (!v.is_number_integer()) bad("must be a whole number");
            const auto d = v.get<long long>();
            if (s.contains("minimum") && d < s["minimum"].get<long long>()) bad("must be at least " + s["minimum"].dump());
            if (s.contains("maximum") && d > s["maximum"].get<long long>()) bad("must be at most " + s["maximum"].dump());
        } else if (type == "boolean") {
            if (!v.is_boolean()) bad("must be true or false");
        } else if (type == "array" && s.value("x-kind", "") == "point") {
            if (!v.is_array() || v.size() != 3) bad("must be three numbers [x, y, z]");
            for (const auto& x : v)
                if (!x.is_number() || !std::isfinite(x.get<double>())) bad("must be three finite numbers [x, y, z]");
        } else if (type == "array") {
            if (!v.is_array()) bad("must be a list");
        } else if (type == "object") {
            if (!v.is_object()) bad("must be an object");
        } else if (type == "string") {
            if (!v.is_string()) bad("must be a string");
            if (s.contains("enum")) {
                bool ok = false;
                for (const auto& e : s["enum"]) ok = ok || e == v;
                if (!ok) bad("must be one of " + s["enum"].dump());
            }
        }
        out[key] = v;
    }
    for (const auto& r : c.schema.value("required", json::array())) {
        if (!out.contains(r.get<std::string>())) throw CommandError("invalid_input", c.name + ": input \"" + r.get<std::string>() + "\" is required");
    }
    return out;
}

json CommandRegistry::run(const std::string& name, const json& params, CommandContext& ctx) const {
    const Command* c = find(name);
    if (!c) throw CommandError("not_found", "No command \"" + name + "\"");
    const json input = validate(*c, params);
    if (!c->mutates) {
        try {
            return c->run(ctx, input);
        } catch (const CommandError&) {
            throw;
        } catch (const std::invalid_argument& e) {
            throw CommandError("invalid_input", e.what());
        } catch (const std::exception& e) {
            throw CommandError("failed", c->name + ": " + e.what());
        }
    }
    ctx.doc.beginChange(c->title.in(ctx.lang));
    try {
        json result = c->run(ctx, input);
        ctx.doc.endChange();
        return result;
    } catch (const CommandError&) {
        ctx.doc.abortChange();
        throw;
    } catch (const std::invalid_argument& e) {
        ctx.doc.abortChange();
        throw CommandError("invalid_input", e.what());
    } catch (const std::exception& e) {
        ctx.doc.abortChange();
        throw CommandError("failed", c->name + ": " + e.what());
    }
}

namespace schema {

namespace {
json described(json j, const Text& d) {
    j["description"] = d.en;
    if (!d.es.empty()) j["x-es"] = d.es;
    return j;
}
}  // namespace

json object(std::initializer_list<std::pair<const char*, json>> props, std::vector<std::string> required) {
    json j = {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}};
    for (const auto& [k, v] : props) j["properties"][k] = v;
    if (!required.empty()) j["required"] = required;
    return j;
}

json body(const Text& d) { return described({{"type", "string"}, {"x-kind", "body"}}, d); }

json string(const Text& d, const char* format) {
    json j = {{"type", "string"}};
    if (format) j["x-kind"] = format;
    return described(j, d);
}

json path(const Text& d, bool save, const char* filter) {
    return described({{"type", "string"}, {"x-kind", save ? "save-path" : "open-path"}, {"x-filter", filter}}, d);
}

json number(const Text& d, double def, double min, double max, const char* unit) {
    json j = {{"type", "number"}, {"default", def}, {"minimum", min}, {"maximum", max}};
    if (unit) j["x-unit"] = unit;
    return described(j, d);
}

json integer(const Text& d, long long def, long long min, long long max) {
    return described({{"type", "integer"}, {"default", def}, {"minimum", min}, {"maximum", max}}, d);
}

json point(const Text& d, const char* unit) {
    json j = {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"x-kind", "point"}};
    if (unit) j["x-unit"] = unit;
    return described(j, d);
}

json boolean(const Text& d, bool def) { return described({{"type", "boolean"}, {"default", def}}, d); }

json choice(const Text& d, std::vector<std::string> values, const std::string& def) {
    return described({{"type", "string"}, {"enum", values}, {"default", def}}, d);
}

}  // namespace schema
}  // namespace orv
