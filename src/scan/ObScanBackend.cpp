// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the Orbbec scanning library CrealityScan installs, loaded at run
// time from CrealityScan's own folder (see ObScanApi.h). OpenRevers builds and
// runs without it; nothing of it is distributed with OpenRevers.
#include "scan/ObScanApi.h"
#include "scan/ScanBackend.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace orv {
namespace fs = std::filesystem;

#ifndef _WIN32

std::unique_ptr<ScanBackend> makeObScanBackend(const fs::path& dir) {
    class Unavailable final : public ScanBackend {
    public:
        explicit Unavailable(fs::path d) : dir_(std::move(d)) {}
        ScanStatus status() override {
            ScanStatus s;
            s.backend = "obscan";
            s.library = (dir_ / "lib_orbbec_scan.dll").string();
            s.error = "The CrealityScan scanning library only runs on Windows.";
            return s;
        }
        std::vector<ScanDevice> devices() override { fail(); }
        void connect(int, const ScanSettings&) override { fail(); }
        void preview() override { fail(); }
        void start() override { fail(); }
        void pause() override { fail(); }
        void stop() override { fail(); }
        std::shared_ptr<TriMesh> cloud(std::size_t, std::vector<Vec3f>*) override { fail(); }
        std::shared_ptr<TriMesh> mesh() override { fail(); }
        void disconnect() override {}

    private:
        [[noreturn]] void fail() { throw ScanError("unavailable", status().error); }
        fs::path dir_;
    };
    return std::make_unique<Unavailable>(dir);
}

#else

namespace {

namespace ob = obscan;

// The library's callbacks carry no user pointer: one scanner per process.
std::atomic<float> g_progress{0};
std::atomic<ob::Status> g_status{ob::kSuccess};
std::atomic<int> g_finished{0};

std::atomic<int> g_deviceMessage{-1};
std::atomic<int> g_deviceMessages{0};

void onProgress(float f) { g_progress = f; }
void onDeviceMessage(ob::DeviceMessage m) {
    g_deviceMessage = m.msg_id;
    ++g_deviceMessages;
}
ob::DeviceMessageListener g_deviceListener{&onDeviceMessage};

const char* deviceMessageName(int id) {
    switch (id) {
    case -1: return "";
    case ob::kDeviceConnect: return "DEVICE_CONNECT";
    case ob::kDeviceDisconnect: return "DEVICE_DISCONNECT";
    case ob::kDeviceConnectNeedCalibration: return "DEVICE_CONNECT_NEED_CALIBRATION";
    case ob::kDeviceConnectWithProblem: return "DEVICE_CONNECT_WITH_PROBLEM";
    case ob::kDeviceConnectReadParamsFailed: return "DEVICE_CONNECT_READ_PARAMS_FAILED";
    case ob::kDeviceConnectCrcCheckFailed: return "DEVICE_CONNECT_CRC_CHECK_FAILED";
    default: return "DEVICE_MESSAGE";
    }
}
void onStatus(ob::Status s) {
    g_status = s;
    ++g_finished;
}

struct Api {
    ob::FnVersion version{};
    ob::FnModeConfigInit modeConfigInit{};
    ob::FnContextCreate contextCreate{};
    ob::FnContextDestroy contextDestroy{};
    ob::FnContextGetDeviceManagement deviceManagement{};
    ob::FnDeviceGetNum deviceNum{};
    ob::FnDeviceGetActiveIdx activeIdx{};
    ob::FnDeviceSelectActive selectActive{};
    ob::FnDeviceGetInfo deviceInfo{};
    ob::FnDeviceMsgListener addDeviceListener{};
    ob::FnDeviceMsgListener removeDeviceListener{};
    ob::FnContextCreateSession createSession{};
    ob::FnSessionDestroy sessionDestroy{};
    ob::FnSessionCheckSupported checkSupported{};
    ob::FnSessionCreateScan createScan{};
    ob::FnScanPreview preview{};
    ob::FnScanStart start{};
    ob::FnScanPause pause{};
    ob::FnScanStop stop{};
    ob::FnScanGetCloud getCloud{};
    ob::FnScanReleaseCloud releaseCloud{};
    ob::FnScanDefaultOptConfig defaultOptConfig{};
    ob::FnScanOptimizeToMesh optimizeToMesh{};
    ob::FnScanGetMesh getMesh{};
};

/// Readable text runs in a block the library filled (its device record:
/// a product id, then fixed-size strings whose sizes are not known yet).
std::vector<std::string> textRuns(const unsigned char* p, std::size_t n) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = p[i];
        if (c >= 32 && c < 127) cur.push_back(static_cast<char>(c));
        else {
            if (cur.size() >= 3) out.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 3) out.push_back(cur);
    return out;
}

class ObScanBackend final : public ScanBackend {
public:
    explicit ObScanBackend(fs::path dir) : dir_(std::move(dir)) {}
    ~ObScanBackend() override {
        try { disconnect(); } catch (...) {}
        if (lib_) FreeLibrary(lib_);
    }

    ScanStatus status() override {
        ScanStatus s;
        s.backend = "obscan";
        s.library = (dir_ / "lib_orbbec_scan.dll").string();
        s.libraryFound = load(false);
        s.error = error_;
        s.version = version_;
        s.state = state_;
        s.device = device_;
        s.points = points_;
        s.progress = g_progress;
        s.lastStatus = ob::statusName(g_status);
        s.deviceMessage = deviceMessageName(g_deviceMessage);
        return s;
    }

    std::vector<ScanDevice> devices() override {
        ensureContext();
        std::int32_t n = 0;
        check(api_.deviceNum(devices_, &n), "get_device_num");
        // The list fills from the device thread after the listener is added:
        // give a scanner already plugged in a few seconds to be announced.
        for (int i = 0; n == 0 && i < 40; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            check(api_.deviceNum(devices_, &n), "get_device_num");
        }
        std::vector<ScanDevice> out;
        for (std::int32_t i = 0; i < n; ++i) {
            std::vector<unsigned char> buf(4096, 0);
            ScanDevice d;
            d.index = i;
            if (api_.deviceInfo(devices_, i, buf.data()) == ob::kSuccess) {
                std::int32_t pid = 0;
                std::memcpy(&pid, buf.data(), 4);
                d.pid = pid;
                const auto runs = textRuns(buf.data() + 4, buf.size() - 4);
                if (!runs.empty()) d.name = runs[0];
                if (runs.size() > 1) d.serial = runs[1];
            }
            out.push_back(d);
        }
        return out;
    }

    void connect(int device, const ScanSettings& st) override {
        ensureContext();
        if (session_) throw ScanError("refused", "already connected; disconnect first");
        std::int32_t n = 0;
        check(api_.deviceNum(devices_, &n), "get_device_num");
        if (n <= 0) throw ScanError("not_found", "no scanner is connected (plug it in and wait a few seconds)");
        if (device >= n) throw ScanError("not_found", "there is no scanner " + std::to_string(device));
        if (device >= 0) check(api_.selectActive(devices_, device), "select_active_device");
        std::int32_t active = 0;
        api_.activeIdx(devices_, &active);
        device_ = active;

        workDir_ = fs::temp_directory_path() / "OpenRevers" / "scan" /
                   std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        fs::create_directories(workDir_);
        const std::string out = workDir_.string(), name = "openrevers";
        const ob::SessionConfig sc{out.c_str(), name.c_str(), 0};
        check(api_.createSession(context_, &sc, &session_), "create_session");

        ob::ScanConfig cfg{};
        api_.modeConfigInit(&cfg.mode_config);
        auto& m = cfg.mode_config;
        m.scan_mode = st.mode == "face" ? ob::kModeFace : st.mode == "human" ? ob::kModeHuman
                    : st.mode == "object_big" ? ob::kModeObjectBig : st.mode == "object_medium" ? ob::kModeObjectMedium
                                                                                                : ob::kModeObjectSmall;
        m.accuracy = st.accuracy == "fast" ? ob::kAccuracyFast : ob::kAccuracyHigh;
        m.object_material = st.material == "black" ? ob::kMaterialBlack : st.material == "white" ? ob::kMaterialWhite
                          : st.material == "reflective" ? ob::kMaterialReflective : ob::kMaterialStandard;
        m.projection = st.light == "blue_laser" ? ob::kBlueLineLaser : ob::kInfrared;
        if (st.voxelSize > 0) m.voxel_size = st.voxelSize / unitScale_;
        cfg.name = name.c_str();
        const ob::Status sup = api_.checkSupported(session_, &cfg);
        if (sup != ob::kSuccess) {
            closeSession();
            throw ScanError("refused", std::string("the scanner does not support these settings (") + ob::statusName(sup) + ")");
        }
        check(api_.createScan(session_, &cfg, &scan_), "create_scan");
        state_ = ScanState::Connected;
        points_ = 0;
    }

    void preview() override {
        need({ScanState::Connected, ScanState::Stopped}, "preview");
        g_progress = 0;
        check(api_.preview(scan_, onProgress, onStatus), "scan_preview");
        state_ = ScanState::Previewing;
    }
    void start() override {
        need({ScanState::Previewing, ScanState::Paused}, "start");
        check(api_.start(scan_), "scan_start");
        state_ = ScanState::Scanning;
    }
    void pause() override {
        need({ScanState::Scanning}, "pause");
        check(api_.pause(scan_, 0), "scan_pause");
        state_ = ScanState::Paused;
    }
    void stop() override {
        need({ScanState::Scanning, ScanState::Paused, ScanState::Previewing}, "stop");
        check(api_.stop(scan_, onProgress, onStatus, 1), "scan_stop");
        state_ = ScanState::Stopped;
    }

    std::shared_ptr<TriMesh> cloud(std::size_t maxPoints, std::vector<Vec3f>* colors) override {
        if (!scan_) throw ScanError("refused", "not connected");
        ob::PointColorClouds c{};
        ob::CloudBbox box{};
        std::uint32_t real = 0;
        auto m = std::make_shared<TriMesh>();
        if (api_.getCloud(scan_, &c, &box, &real) != ob::kSuccess) return m;
        const std::size_t n = std::min<std::size_t>(c.num_points, real ? real : c.num_points);
        points_ = n;
        decideUnit(box, n);
        const std::size_t step = maxPoints > 0 && n > maxPoints ? (n + maxPoints - 1) / maxPoints : 1;
        m->vertices.reserve(n / step + 1);
        for (std::size_t i = 0; c.points && i < n; i += step) {
            const auto& p = c.points[i];
            m->vertices.emplace_back(p.x * unitScale_, p.y * unitScale_, p.z * unitScale_);
            if (c.normals) m->normals.emplace_back(c.normals[i].nx, c.normals[i].ny, c.normals[i].nz);
            if (colors && c.colors) colors->emplace_back(c.colors[i].r / 255.f, c.colors[i].g / 255.f, c.colors[i].b / 255.f);
        }
        api_.releaseCloud(scan_);
        if (!m->hasNormals()) m->normals.clear();
        return m;
    }

    std::shared_ptr<TriMesh> mesh() override {
        need({ScanState::Stopped}, "make the mesh");
        ob::OptimizationConfig oc{};
        check(api_.defaultOptConfig(scan_, &oc), "get_default_opt_to_mesh_config");
        const int before = g_finished;
        g_progress = 0;
        state_ = ScanState::Meshing;
        if (api_.optimizeToMesh(scan_, onProgress, oc, onStatus) != ob::kSuccess) {
            state_ = ScanState::Stopped;
            throw ScanError("failed", std::string("the library could not mesh the scan (") + ob::statusName(g_status) + ")");
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(20);
        while (g_finished == before && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        state_ = ScanState::Stopped;
        if (g_finished == before) throw ScanError("failed", "the library took more than 20 minutes to mesh the scan");
        if (g_status != ob::kSuccess) throw ScanError("failed", std::string("meshing failed: ") + ob::statusName(g_status));
        ob::TriangleMesh t{};
        check(api_.getMesh(scan_, &t), "get_optimization_mesh");
        auto m = std::make_shared<TriMesh>();
        m->vertices.reserve(t.num_vertices);
        for (std::uint32_t i = 0; t.vertices && i < t.num_vertices; ++i)
            m->vertices.emplace_back(t.vertices[i].x * unitScale_, t.vertices[i].y * unitScale_, t.vertices[i].z * unitScale_);
        m->faces.reserve(t.num_triangles);
        for (std::uint32_t i = 0; t.triangles && i < t.num_triangles; ++i) {
            const auto& f = t.triangles[i];
            if (f.v0 < t.num_vertices && f.v1 < t.num_vertices && f.v2 < t.num_vertices) m->faces.push_back({f.v0, f.v1, f.v2});
        }
        return m;
    }

    void disconnect() override {
        if (scan_ && (state_ == ScanState::Scanning || state_ == ScanState::Paused || state_ == ScanState::Previewing))
            api_.stop(scan_, onProgress, onStatus, 1);
        closeSession();
        if (devices_) api_.removeDeviceListener(devices_, &g_deviceListener);
        if (context_) api_.contextDestroy(context_);
        context_ = devices_ = nullptr;
        state_ = ScanState::Disconnected;
        device_ = -1;
        points_ = 0;
    }

private:
    bool load(bool mustWork) {
        if (lib_) return true;
        const fs::path dll = dir_ / "lib_orbbec_scan.dll";
        if (!fs::exists(dll)) {
            error_ = "lib_orbbec_scan.dll not found in " + dir_.string() + " (is CrealityScan installed? set ORV_CREALITYSCAN_DIR to its Plugins\\x86_64 folder)";
            if (mustWork) throw ScanError("unavailable", error_);
            return false;
        }
        // Its own folder first for what it needs (OrbbecSDK.dll and the rest).
        lib_ = LoadLibraryExW(dll.wstring().c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!lib_) {
            error_ = "could not load " + dll.string() + " (Windows error " + std::to_string(GetLastError()) + ")";
            if (mustWork) throw ScanError("unavailable", error_);
            return false;
        }
        std::string missing;
        auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(reinterpret_cast<void*>(GetProcAddress(lib_, name)));
            if (!fn) missing += std::string(missing.empty() ? "" : ", ") + name;
        };
        get(api_.version, "obscan_version");
        get(api_.modeConfigInit, "obscan_mode_config_init");
        get(api_.contextCreate, "obscan_context_create");
        get(api_.contextDestroy, "obscan_context_destroy");
        get(api_.deviceManagement, "obscan_context_get_device_management");
        get(api_.deviceNum, "obscan_device_management_get_device_num");
        get(api_.activeIdx, "obscan_device_management_get_active_idx");
        get(api_.selectActive, "obscan_device_management_select_active_device");
        get(api_.deviceInfo, "obscan_device_management_get_device_info");
        get(api_.addDeviceListener, "obscan_device_management_add_device_msg_listener");
        get(api_.removeDeviceListener, "obscan_device_management_remove_device_msg_listener");
        get(api_.createSession, "obscan_context_create_session");
        get(api_.sessionDestroy, "obscan_session_destroy");
        get(api_.checkSupported, "obscan_session_check_supported");
        get(api_.createScan, "obscan_session_create_scan");
        get(api_.preview, "obscan_scan_preview");
        get(api_.start, "obscan_scan_start");
        get(api_.pause, "obscan_scan_pause");
        get(api_.stop, "obscan_scan_stop");
        get(api_.getCloud, "obscan_scan_get_model_point_color_clouds");
        get(api_.releaseCloud, "obscan_scan_model_clouds_release");
        get(api_.defaultOptConfig, "obscan_scan_get_default_opt_to_mesh_config");
        get(api_.optimizeToMesh, "obscan_scan_optimization_to_mesh");
        get(api_.getMesh, "obscan_scan_get_optimization_mesh");
        if (!missing.empty()) {
            error_ = "this version of lib_orbbec_scan.dll lacks: " + missing;
            FreeLibrary(lib_);
            lib_ = nullptr;
            if (mustWork) throw ScanError("unavailable", error_);
            return false;
        }
        // obscan_version's output is a pointer whose meaning is not confirmed
        // yet (a string, most likely): not read until checked on a scanner.
        version_ = "lib_orbbec_scan.dll";
        error_.clear();
        return true;
    }

    void ensureContext() {
        load(true);
        if (context_) return;
        const fs::path logs = fs::temp_directory_path() / "OpenRevers" / "scan-logs";
        fs::create_directories(logs);
        const std::string algo = dir_.string(), l = logs.string();
        const ob::ContextConfig cc{algo.c_str(), l.c_str(), l.c_str(), l.c_str(), l.c_str()};
        check(api_.contextCreate(&context_, &cc), "context_create");
        check(api_.deviceManagement(context_, &devices_), "get_device_management");
        check(api_.addDeviceListener(devices_, &g_deviceListener), "add_device_msg_listener");
    }

    void closeSession() {
        if (session_) api_.sessionDestroy(session_);
        session_ = scan_ = nullptr;
    }

    /// The library may give metres: a model smaller than 5 units across with
    /// many points is taken as metres. Decided once per connection.
    void decideUnit(const ob::CloudBbox& b, std::size_t n) {
        if (unitDecided_ || n < 1000) return;
        const float dx = b.max.x - b.min.x, dy = b.max.y - b.min.y, dz = b.max.z - b.min.z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        unitScale_ = d > 0 && d < 5 ? 1000.f : 1.f;
        unitDecided_ = true;
    }

    void need(std::initializer_list<ScanState> ok, const char* what) const {
        for (ScanState s : ok)
            if (state_ == s) return;
        throw ScanError("refused", std::string("cannot ") + what + " while " + scanStateName(state_));
    }

    void check(ob::Status s, const char* what) {
        if (s != ob::kSuccess) throw ScanError("failed", std::string("obscan_") + what + " failed: " + ob::statusName(s));
    }

    fs::path dir_, workDir_;
    HMODULE lib_{nullptr};
    Api api_;
    std::string error_, version_;
    void* context_{nullptr};
    void* devices_{nullptr};
    void* session_{nullptr};
    void* scan_{nullptr};
    ScanState state_{ScanState::Disconnected};
    int device_{-1};
    std::size_t points_{0};
    float unitScale_{1.f};
    bool unitDecided_{false};
};

}  // namespace

std::unique_ptr<ScanBackend> makeObScanBackend(const fs::path& dir) { return std::make_unique<ObScanBackend>(dir); }

#endif

}  // namespace orv
