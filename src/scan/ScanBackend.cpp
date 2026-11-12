// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the scanner of the process, and the simulator.
#include "scan/ScanBackend.h"

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <random>

namespace orv {

const char* scanStateName(ScanState s) {
    switch (s) {
        case ScanState::Disconnected: return "disconnected";
        case ScanState::Connected: return "connected";
        case ScanState::Previewing: return "previewing";
        case ScanState::Scanning: return "scanning";
        case ScanState::Paused: return "paused";
        case ScanState::Stopped: return "stopped";
        case ScanState::Meshing: return "meshing";
        case ScanState::Failed: return "failed";
    }
    return "unknown";
}

std::filesystem::path defaultScanLibraryDir() {
    if (const char* dir = std::getenv("ORV_CREALITYSCAN_DIR"); dir && *dir) return dir;
    return std::filesystem::path("C:/Program Files/CrealityScan/CrealityScan_Data/Plugins/x86_64");
}

ScanManager& ScanManager::instance() {
    static ScanManager m;
    return m;
}

ScanBackend& ScanManager::backend() {
    std::lock_guard g(m_);
    if (!backend_) {
        const char* sim = std::getenv("ORV_SCAN_SIMULATOR");
        if (sim && std::string(sim) == "1") backend_ = makeSimulatedBackend();
        else backend_ = makeObScanBackend(defaultScanLibraryDir());
    }
    return *backend_;
}

void ScanManager::use(const std::string& kind, const std::filesystem::path& libraryDir) {
    std::lock_guard g(m_);
    if (backend_) {
        try { backend_->disconnect(); } catch (...) {}
    }
    backend_.reset();
    if (kind == "simulator") backend_ = makeSimulatedBackend();
    else backend_ = makeObScanBackend(libraryDir.empty() ? defaultScanLibraryDir() : libraryDir);
}

namespace {

/// A pretend scanner: a 60 × 40 × 8 mm plate with a Ø10 hole through it,
/// seen a little more at each look while it "scans".
class SimulatedBackend final : public ScanBackend {
public:
    ScanStatus status() override {
        ScanStatus s;
        s.libraryFound = true;
        s.backend = "simulator";
        s.library = "(simulator)";
        s.version = "simulator 1";
        s.state = state_;
        s.device = device_;
        s.points = seen_;
        s.progress = progress_;
        s.lastStatus = "SUCCESS";
        return s;
    }
    std::vector<ScanDevice> devices() override { return {{0, 0x0669, "Simulated scanner", "SIM-0001"}}; }
    void connect(int device, const ScanSettings&) override {
        if (device > 0) throw ScanError("not_found", "no device " + std::to_string(device));
        device_ = 0;
        state_ = ScanState::Connected;
        seen_ = 0;
    }
    void preview() override { need({ScanState::Connected, ScanState::Stopped}, "preview"); state_ = ScanState::Previewing; }
    void start() override { need({ScanState::Previewing, ScanState::Paused, ScanState::Connected}, "start"); state_ = ScanState::Scanning; }
    void pause() override { need({ScanState::Scanning}, "pause"); state_ = ScanState::Paused; }
    void stop() override {
        need({ScanState::Scanning, ScanState::Paused, ScanState::Previewing}, "stop");
        state_ = ScanState::Stopped;
        progress_ = 1;
    }
    std::shared_ptr<TriMesh> cloud(std::size_t maxPoints, std::vector<Vec3f>* colors) override {
        if (state_ == ScanState::Disconnected) throw ScanError("refused", "not connected");
        if (state_ == ScanState::Scanning) seen_ = std::min<std::size_t>(kTotal, seen_ + kTotal / 4);
        auto m = std::make_shared<TriMesh>();
        const std::size_t n = seen_;
        const std::size_t step = maxPoints > 0 && n > maxPoints ? (n + maxPoints - 1) / maxPoints : 1;
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        std::normal_distribution<float> noise(0.f, 0.02f);
        for (std::size_t i = 0; i < n; ++i) {
            Vec3f p, nrm;
            sample(u, rng, p, nrm);
            if (i % step) continue;
            m->vertices.push_back(p + nrm * noise(rng));
            m->normals.push_back(nrm);
            if (colors) colors->push_back(Vec3f(0.8f, 0.8f, 0.78f));
        }
        return m;
    }
    std::shared_ptr<TriMesh> mesh() override {
        need({ScanState::Stopped}, "mesh");
        auto m = std::make_shared<TriMesh>();
        const float X = 30, Y = 20, Z = 8;
        m->vertices = {{-X, -Y, 0}, {X, -Y, 0}, {X, Y, 0}, {-X, Y, 0}, {-X, -Y, Z}, {X, -Y, Z}, {X, Y, Z}, {-X, Y, Z}};
        m->faces = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
        return m;
    }
    void disconnect() override {
        state_ = ScanState::Disconnected;
        device_ = -1;
        seen_ = 0;
    }

private:
    static constexpr std::size_t kTotal = 40000;

    void need(std::initializer_list<ScanState> ok, const char* what) const {
        for (ScanState s : ok)
            if (state_ == s) return;
        throw ScanError("refused", std::string("cannot ") + what + " while " + scanStateName(state_));
    }

    /// A point on the plate's surface, chosen by area.
    static void sample(std::uniform_real_distribution<float>& u, std::mt19937& rng, Vec3f& p, Vec3f& n) {
        const float X = 30, Y = 20, Z = 8, R = 5;
        const float top = 4 * X * Y, side = 2 * (2 * X + 2 * Y) * Z, hole = 2 * std::numbers::pi_v<float> * R * Z;
        for (;;) {
            float t = u(rng) * (2 * top + side + hole);
            if (t < 2 * top) {
                p = Vec3f((u(rng) * 2 - 1) * X, (u(rng) * 2 - 1) * Y, t < top ? Z : 0.f);
                if (p.head<2>().norm() < R) continue;
                n = Vec3f(0, 0, t < top ? 1.f : -1.f);
                return;
            }
            t -= 2 * top;
            if (t < side) {
                const float z = u(rng) * Z, s = u(rng) * (4 * X + 4 * Y);
                if (s < 2 * X) { p = Vec3f(s - X, -Y, z); n = Vec3f(0, -1, 0); }
                else if (s < 4 * X) { p = Vec3f(s - 3 * X, Y, z); n = Vec3f(0, 1, 0); }
                else if (s < 4 * X + 2 * Y) { p = Vec3f(-X, s - 4 * X - Y, z); n = Vec3f(-1, 0, 0); }
                else { p = Vec3f(X, s - 4 * X - 3 * Y, z); n = Vec3f(1, 0, 0); }
                return;
            }
            const float a = u(rng) * 2 * std::numbers::pi_v<float>;
            p = Vec3f(R * std::cos(a), R * std::sin(a), u(rng) * Z);
            n = Vec3f(-std::cos(a), -std::sin(a), 0);  // into the hole
            return;
        }
    }

    ScanState state_{ScanState::Disconnected};
    int device_{-1};
    std::size_t seen_{0};
    float progress_{0};
};

}  // namespace

std::unique_ptr<ScanBackend> makeSimulatedBackend() { return std::make_unique<SimulatedBackend>(); }

}  // namespace orv
