// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — scanning straight from OpenRevers (phase 6). A ScanBackend
// drives one scanner: the Orbbec scanning library CrealityScan installs
// (loaded at run time, Windows only) or a simulator for tests and demos.
// The ScanManager keeps the one scanner of the process and is what the
// scan.* commands talk to. No Qt here.
#pragma once

#include "core/mesh/TriMesh.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace orv {

struct ScanDevice {
    int index{0};
    int pid{0};             // USB product id
    std::string name;       // as the library reports it (may be empty)
    std::string serial;
};

enum class ScanState { Disconnected, Connected, Previewing, Scanning, Paused, Stopped, Meshing, Failed };
const char* scanStateName(ScanState s);

struct ScanSettings {
    std::string mode{"object_small"};  // object_small, object_medium, object_big, face, human
    std::string accuracy{"high"};      // high, fast
    std::string material{"standard"};  // standard, black, white, reflective
    std::string light{"infrared"};     // infrared, blue_laser
    double voxelSize{0};               // mm; 0: the library's default
};

struct ScanStatus {
    bool libraryFound{false};
    std::string backend;       // "obscan", "simulator" or "none"
    std::string library;       // the DLL loaded (or looked for)
    std::string version;
    std::string error;         // why the library is not usable
    ScanState state{ScanState::Disconnected};
    int device{-1};
    std::size_t points{0};     // in the model so far
    float progress{0};         // of the last long task (0..1)
    std::string lastStatus;    // the library's last status, by name
};

class ScanBackend {
public:
    virtual ~ScanBackend() = default;
    virtual ScanStatus status() = 0;
    virtual std::vector<ScanDevice> devices() = 0;
    virtual void connect(int device, const ScanSettings& settings) = 0;
    virtual void preview() = 0;
    virtual void start() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    /// The model's points so far, in millimetres (at most maxPoints, evenly
    /// thinned). Colours, when the scanner gives them, in `colors` (0..1).
    virtual std::shared_ptr<TriMesh> cloud(std::size_t maxPoints, std::vector<Vec3f>* colors = nullptr) = 0;
    /// After stop: the library's own mesh of the scan, in millimetres.
    virtual std::shared_ptr<TriMesh> mesh() = 0;
    virtual void disconnect() = 0;
};

/// Thrown by a backend: `code` is a CommandError code (unavailable, refused, failed).
struct ScanError : std::runtime_error {
    ScanError(std::string c, const std::string& m) : std::runtime_error(m), code(std::move(c)) {}
    std::string code;
};

/// The folder where CrealityScan keeps lib_orbbec_scan.dll: ORV_CREALITYSCAN_DIR
/// if set, else its default install folder.
std::filesystem::path defaultScanLibraryDir();

std::unique_ptr<ScanBackend> makeObScanBackend(const std::filesystem::path& libraryDir);
std::unique_ptr<ScanBackend> makeSimulatedBackend();

/// The scanner of this process. Picks the simulator when ORV_SCAN_SIMULATOR=1
/// (or when asked), the Orbbec library otherwise.
class ScanManager {
public:
    static ScanManager& instance();
    /// The backend in use, created on first use. Not thread-safe by itself:
    /// hold lock() while using it.
    ScanBackend& backend();
    /// Replace the backend ("obscan" with an optional folder, or "simulator").
    void use(const std::string& kind, const std::filesystem::path& libraryDir = {});
    std::unique_lock<std::recursive_mutex> lock() { return std::unique_lock(m_); }
    std::unique_lock<std::recursive_mutex> tryLock() { return std::unique_lock(m_, std::try_to_lock); }
    /// Whether a backend was created (the view only polls one in use).
    bool started() const { return backend_ != nullptr; }

private:
    std::recursive_mutex m_;
    std::unique_ptr<ScanBackend> backend_;
};

}  // namespace orv
