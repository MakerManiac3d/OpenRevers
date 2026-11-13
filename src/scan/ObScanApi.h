// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — our own declarations of the C interface of lib_orbbec_scan.dll,
// the scanning library CrealityScan installs for its Orbbec-made scanners
// (Creality Raptor, Otter, Ferret...). Written for interoperability from the
// names, parameter types and structure layouts recorded in CrealityScan's
// IL2CPP metadata (see docs/desarrollo/escaneo-obscan.md); no Creality or
// Orbbec code or header is copied here, and no library is distributed:
// OpenRevers loads the one the user's CrealityScan installed, at run time.
//
// Only what OpenRevers calls is declared. Every function returns an
// ObScanStatus; 0 is success. All handles are opaque pointers.
#pragma once

#include <cstdint>

namespace orv::obscan {

using Status = std::int32_t;
constexpr Status kSuccess = 0;

/// Names of the status values, in the order the metadata lists them (the
/// values are taken as 0, 1, 2...: to be confirmed on a real scanner).
inline const char* statusName(Status s) {
    static const char* const kNames[] = {
        "SUCCESS", "INVALID_ARGUMENT", "FATAL", "DEVICE_DISCONNECT", "SESSION_PAUSED", "SESSION_NOT_PAUSED",
        "UNSUPPORTED_CONFIGURATION", "CAMERA_PERMISSION_NOT_GRANTED", "DEADLINE_EXCEEDED", "CAMERA_STREAMING_RETRY_FAILED",
        "LISTENER_NOT_FOUND", "MULTI_CALLED", "CAMERA_NOT_REGISTER", "OUT_MEMORY", "FILE_NOT_EXIST", "FILE_READ_FAILED",
        "FILE_WRITE_FAILED", "GRAPHICS_PROBLEM", "HAVE_NOT_MARKER", "IMPORT_EMPTY_PROJ", "IMPORT_SELF_PROJ",
        "NO_PLUG_IN_POWER", "INVALID_SCAN", "DEVICE_BUSY", "CANCEL_RUN", "CAMERA_PARAMS_INCONSISTENT",
        "LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN", "OUT_HARD_DISK", "READ_DEVICE_PARAMS", "IMPORT_NOT_SUPPORT_PROJ",
        "IMPORT_HIGH_VERSION_PROJ", "RESTORE_FACTORY_CRC_CHECK_FAILED", "RESTORE_FACTORY_DATA_WRITE_FAILED",
        "AI_HEAD_RESTORATION_NOT_DETECT_FACE", "MESH_NOT_FOUND_HOLE", "PROJ_NOT_WRITE_PERMISSION", "SET_LED_LASER",
        "LOADING_INTR_LASER_PARAMS", "SET_IR_MODE", "AI_REPLACE_RGB_FEATS_NOT_OBVIOUS", "IMPORTED_MESH_SIZE_NOT_SUPPORT",
        "MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION", "ALLOCATE_GPU_RESOURCES_FAILED"};
    constexpr int n = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
    return s >= 0 && s < n ? kNames[s] : "UNKNOWN";
}

// Enumerations (32-bit), values in metadata order.
enum Projection : std::int32_t { kInfrared = 0, kBlueLineLaser = 1 };
enum ScanMode : std::int32_t { kModeFace = 0, kModeObjectSmall, kModeObjectMedium, kModeObjectBig, kModeHuman };
enum Accuracy : std::int32_t { kAccuracyFast = 0, kAccuracyHigh = 1 };
enum ObjectMaterial : std::int32_t { kMaterialStandard = 0, kMaterialBlack, kMaterialWhite, kMaterialReflective };

struct Point {  // millimetres or metres: see ScanBackend (the unit is checked)
    float x, y, z, intensity;
};
struct Normal {
    float nx, ny, nz, nw;
};
struct Color {
    std::uint8_t r, g, b, a;
};
struct Triangle {
    std::uint32_t v0, v1, v2;
};
struct PointColorClouds {
    std::uint32_t num_points;
    Point* points;
    Normal* normals;
    Color* colors;
};
struct TriangleMesh {
    std::uint32_t num_vertices;
    Point* vertices;
    Normal* normals;
    std::uint32_t num_triangles;
    Triangle* triangles;
};
struct CloudBbox {
    Point min, max, center;
};

struct ModeConfig {
    std::int32_t post_texture;
    std::int32_t frame_rate;
    std::int32_t projection;
    std::int32_t scan_mode;
    std::int32_t accuracy;
    std::int32_t fuse_mode;
    std::int32_t scan_fov;
    std::int32_t object_material;
    double voxel_size;
    std::int32_t light_type;
    std::int32_t scan_type;
    double detail_voxel_size;
};
struct ScanConfig {
    ModeConfig mode_config;
    const char* name;
};
struct ContextConfig {
    const char* input_algo_dependent_files_dir;
    const char* output_ob_sdk_log_dir;
    const char* output_ob_scan_log_dir;
    const char* output_computer_performance_fps_dir;
    const char* output_windows_gpu_info_dir;
};
struct SessionConfig {
    const char* output_dir;
    const char* name;
    std::int32_t is_import_mesh_file;  // a 4-byte BOOL
};
struct OptimizationConfig {  // passed by value
    std::int32_t compute_speed;
    float voxel_size;
    float credible_ratio;
    std::int32_t remove_markers;  // a 4-byte BOOL
    std::int32_t export_format;
};

// Device messages (OBScanDeviceMessageID, in the metadata's order). The
// library only fills its scanner list once a device listener is registered:
// without one it logs "device callback is nullptr" and the list stays empty.
enum DeviceMessageId : std::int32_t {
    kDeviceConnect = 0,
    kDeviceDisconnect = 1,
    kDeviceConnectNeedCalibration = 6,
    kDeviceConnectWithProblem = 8,
    kDeviceConnectReadParamsFailed = 9,
    kDeviceConnectCrcCheckFailed = 10,
};
struct DeviceMessage {  // passed by value (16 bytes: by address on x64)
    std::int32_t msg_id;
    void* data;
};
using DeviceMessageCallback = void (*)(DeviceMessage message);
struct DeviceMessageListener {  // the library keeps its address
    DeviceMessageCallback onMessage;
};

using ProgressCallback = void (*)(float fraction);
using StatusCallback = void (*)(Status status);

// The functions OpenRevers resolves, by name. Nothing else is looked up, so
// the library's firmware, Wi-Fi, factory-reset, registration and "AI"
// functions (some of which use the network) can never be called from here.
using FnVersion = Status (*)(const char** version);
using FnModeConfigInit = Status (*)(ModeConfig* config);
using FnContextCreate = Status (*)(void** context, const ContextConfig* config);
using FnContextDestroy = Status (*)(void* context);
using FnContextGetDeviceManagement = Status (*)(void* context, void** devices);
using FnDeviceGetNum = Status (*)(void* devices, std::int32_t* count);
using FnDeviceGetActiveIdx = Status (*)(void* devices, std::int32_t* index);
using FnDeviceSelectActive = Status (*)(void* devices, std::int32_t index);
using FnDeviceGetInfo = Status (*)(void* devices, std::int32_t index, void* info);
using FnDeviceMsgListener = Status (*)(void* devices, DeviceMessageListener* listener);
using FnContextCreateSession = Status (*)(void* context, const SessionConfig* config, void** session);
using FnSessionDestroy = Status (*)(void* session);
using FnSessionCheckSupported = Status (*)(void* session, const ScanConfig* config);
using FnSessionCreateScan = Status (*)(void* session, const ScanConfig* config, void** scan);
using FnScanPreview = Status (*)(void* scan, ProgressCallback progress, StatusCallback status);
using FnScanStart = Status (*)(void* scan);
using FnScanPause = Status (*)(void* scan, std::int32_t stopStream);
using FnScanStop = Status (*)(void* scan, ProgressCallback progress, StatusCallback status, std::int32_t block);
using FnScanGetCloud = Status (*)(void* scan, PointColorClouds* cloud, CloudBbox* bbox, std::uint32_t* realPoints);
using FnScanReleaseCloud = Status (*)(void* scan);
using FnScanDefaultOptConfig = Status (*)(void* scan, OptimizationConfig* config);
using FnScanOptimizeToMesh = Status (*)(void* scan, ProgressCallback progress, OptimizationConfig config, StatusCallback status);
using FnScanGetMesh = Status (*)(void* scan, TriangleMesh* mesh);

/// Every name resolved; the test suite checks no forbidden one sneaks in.
inline constexpr const char* kResolvedFunctions[] = {
    "obscan_version",
    "obscan_mode_config_init",
    "obscan_context_create",
    "obscan_context_destroy",
    "obscan_context_get_device_management",
    "obscan_device_management_get_device_num",
    "obscan_device_management_get_active_idx",
    "obscan_device_management_select_active_device",
    "obscan_device_management_get_device_info",
    "obscan_device_management_add_device_msg_listener",
    "obscan_device_management_remove_device_msg_listener",
    "obscan_context_create_session",
    "obscan_session_destroy",
    "obscan_session_check_supported",
    "obscan_session_create_scan",
    "obscan_scan_preview",
    "obscan_scan_start",
    "obscan_scan_pause",
    "obscan_scan_stop",
    "obscan_scan_get_model_point_color_clouds",
    "obscan_scan_model_clouds_release",
    "obscan_scan_get_default_opt_to_mesh_config",
    "obscan_scan_optimization_to_mesh",
    "obscan_scan_get_optimization_mesh",
};

/// Parts of names OpenRevers must never resolve (network, firmware, device
/// settings, registration, generative "AI").
inline constexpr const char* kForbiddenFragments[] = {
    "AI_", "upgrade", "wifi", "factory", "register_state", "recovery", "reboot", "backup_device", "resume_device", "work_mode", "3D_GS",
};

}  // namespace orv::obscan
