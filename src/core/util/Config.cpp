// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Config.h"

#include <cwchar>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>

namespace orv {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
std::string env(const char* name) {
#ifdef _MSC_VER
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, name) || !v) return {};
    std::string s(v);
    free(v);
    return s;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

std::string newToken() {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::uniform_int_distribution<int> pick(0, static_cast<int>(sizeof(alphabet)) - 2);
    std::string t;
    for (int i = 0; i < 32; ++i) t += alphabet[pick(rd)];
    return t;
}

fs::path normalised(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(fs::absolute(p, ec), ec);
    if (ec) abs = fs::absolute(p).lexically_normal();
    return abs;
}

bool inside(const fs::path& p, const fs::path& root) {
    auto r = root.begin(), x = p.begin();
    for (; r != root.end(); ++r, ++x) {
        if (x == p.end()) return false;
#ifdef _WIN32
        std::wstring a = r->wstring(), b = x->wstring();
        if (_wcsicmp(a.c_str(), b.c_str()) != 0) {
            if (a.empty()) continue;  // trailing separator
            return false;
        }
#else
        if (*r != *x) {
            if (r->empty()) continue;
            return false;
        }
#endif
    }
    return true;
}
}  // namespace

fs::path homeDir() {
#ifdef _WIN32
    std::string h = env("USERPROFILE");
#else
    std::string h = env("HOME");
#endif
    return h.empty() ? fs::current_path() : fs::path(h);
}

fs::path configDir() {
    if (auto o = env("OPENREVERS_CONFIG_DIR"); !o.empty()) return o;
#ifdef _WIN32
    std::string a = env("APPDATA");
    return (a.empty() ? homeDir() / "AppData" / "Roaming" : fs::path(a)) / "OpenRevers";
#elif defined(__APPLE__)
    return homeDir() / "Library" / "Application Support" / "OpenRevers";
#else
    std::string x = env("XDG_CONFIG_HOME");
    return (x.empty() ? homeDir() / ".config" : fs::path(x)) / "OpenRevers";
#endif
}

AgentSettings loadAgentSettings() {
    AgentSettings s;
    const fs::path file = configDir() / "agent.json";
    bool dirty = false;
    try {
        std::ifstream in(file);
        if (in) {
            json j = json::parse(in);
            s.port = j.value("port", s.port);
            s.token = j.value("token", std::string());
            s.enabled = j.value("enabled", true);
            s.experimentalScan = j.value("experimentalScan", false);
            for (const auto& f : j.value("allowedFolders", json::array())) s.allowedFolders.emplace_back(f.get<std::string>());
        } else {
            dirty = true;
        }
    } catch (const std::exception&) {
        dirty = true;  // a damaged file is replaced with a fresh one
    }
    if (s.token.size() < 16) {
        s.token = newToken();
        dirty = true;
    }
    if (dirty) {
        try {
            saveAgentSettings(s);
        } catch (const std::exception&) {
        }
    }
    return s;
}

void saveAgentSettings(const AgentSettings& s) {
    const fs::path dir = configDir();
    fs::create_directories(dir);
    json j;
    j["port"] = s.port;
    j["token"] = s.token;
    j["enabled"] = s.enabled;
    j["experimentalScan"] = s.experimentalScan;
    j["allowedFolders"] = json::array();
    for (const auto& f : s.allowedFolders) j["allowedFolders"].push_back(f.string());
    std::ofstream out(dir / "agent.json", std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write " + (dir / "agent.json").string());
    out << j.dump(2) << '\n';
}

PathPolicy PathPolicy::unrestricted() { return PathPolicy(); }

PathPolicy PathPolicy::forAgent(const AgentSettings& s) {
    PathPolicy p;
    p.restricted_ = true;
    if (s.allowedFolders.empty()) p.roots_.push_back(normalised(homeDir()));
    for (const auto& f : s.allowedFolders) p.roots_.push_back(normalised(f));
    return p;
}

fs::path PathPolicy::check(const std::string& path, bool forWriting) const {
    if (path.empty()) throw std::invalid_argument("No path given");
    const fs::path p = normalised(fs::path(path));
    if (!restricted_) return p;
    for (const auto& r : roots_)
        if (inside(p, r)) return p;
    std::string list;
    for (const auto& r : roots_) list += (list.empty() ? "" : ", ") + r.string();
    throw std::invalid_argument(std::string("The agent may not ") + (forWriting ? "write to " : "read ") + p.string() +
                                ": it is outside the folders allowed for agents (" + list +
                                "). Add the folder in OpenRevers → Settings → AI agent, or in " + (configDir() / "agent.json").string() + ".");
}

}  // namespace orv
