// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — settings shared by the application and the agent bridge:
// where they live, the local agent port and token, and which folders an AI
// agent may read and write.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace orv {

/// %APPDATA%\OpenRevers on Windows, ~/Library/Application Support/OpenRevers on
/// macOS, $XDG_CONFIG_HOME/OpenRevers (or ~/.config/OpenRevers) elsewhere.
/// OPENREVERS_CONFIG_DIR overrides it (tests, portable installs).
std::filesystem::path configDir();

/// The user's home folder.
std::filesystem::path homeDir();

struct AgentSettings {
    int port{47823};                 // 127.0.0.1 only
    std::string token;               // made on first use
    /// Folders the agent may read from and write to (and everything below).
    /// Empty means: the user's home folder.
    std::vector<std::filesystem::path> allowedFolders;
    bool enabled{true};
    /// Direct scanning through CrealityScan's library (experimental, off by
    /// default): its commands and the Scan menu only exist when on.
    bool experimentalScan{false};
};

/// Loads agent.json from configDir(), creating it (with a new token) if missing.
AgentSettings loadAgentSettings();
void saveAgentSettings(const AgentSettings& s);

/// Who asks for a path decides how much is checked: the user in the
/// interface may go anywhere; an agent only inside its allowed folders.
class PathPolicy {
public:
    static PathPolicy unrestricted();
    static PathPolicy forAgent(const AgentSettings& s);

    /// The absolute, normalised path; throws std::invalid_argument when it
    /// lies outside the allowed folders.
    std::filesystem::path check(const std::string& path, bool forWriting) const;
    bool restricted() const { return restricted_; }
    const std::vector<std::filesystem::path>& roots() const { return roots_; }

private:
    bool restricted_{false};
    std::vector<std::filesystem::path> roots_;
};

}  // namespace orv
