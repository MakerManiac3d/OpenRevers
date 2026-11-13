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

/// Names of the status values. Only SUCCESS is 0: the errors are coded
/// 0x326201xx (read from the constants in CrealityScan's metadata).
inline const char* statusName(Status s) {
    struct Entry { Status value; const char* name; };
    static const Entry kNames[] = {
        {0, "SUCCESS"},
        {0x32620100, "INVALID_ARGUMENT"},
        {0x32620101, "FATAL"},
        {0x32620102, "DEVICE_DISCONNECT"},
        {0x32620103, "SESSION_PAUSED"},
        {0x32620104, "SESSION_NOT_PAUSED"},
        {0x32620105, "UNSUPPORTED_CONFIGURATION"},
        {0x32620106, "CAMERA_PERMISSION_NOT_GRANTED"},
        {0x32620107, "DEADLINE_EXCEEDED"},
        {0x32620108, "CAMERA_STREAMING_RETRY_FAILED"},
        {0x32620109, "LISTENER_NOT_FOUND"},
        {0x32620110, "MULTI_CALLED"},
        {0x32620111, "CAMERA_NOT_REGISTER"},
        {0x32620112, "OUT_MEMORY"},
        {0x32620113, "FILE_NOT_EXIST"},
        {0x32620114, "FILE_READ_FAILED"},
        {0x32620115, "FILE_WRITE_FAILED"},
        {0x32620116, "GRAPHICS_PROBLEM"},
        {0x32620117, "HAVE_NOT_MARKER"},
        {0x32620118, "IMPORT_EMPTY_PROJ"},
        {0x32620119, "IMPORT_SELF_PROJ"},
        {0x32620121, "NO_PLUG_IN_POWER"},
        {0x32620122, "INVALID_SCAN"},
        {0x32620123, "DEVICE_BUSY"},
        {0x32620124, "CANCEL_RUN"},
        {0x32620125, "CAMERA_PARAMS_INCONSISTENT"},
        {0x32620126, "LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN"},
        {0x32620127, "OUT_HARD_DISK"},
        {0x32620128, "READ_DEVICE_PARAMS"},
        {0x32620129, "IMPORT_NOT_SUPPORT_PROJ"},
        {0x32620130, "IMPORT_HIGH_VERSION_PROJ"},
        {0x32620131, "RESTORE_FACTORY_CRC_CHECK_FAILED"},
        {0x32620132, "RESTORE_FACTORY_DATA_WRITE_FAILED"},
        {0x32620133, "AI_HEAD_RESTORATION_NOT_DETECT_FACE"},
        {0x32620134, "MESH_NOT_FOUND_HOLE"},
        {0x32620135, "PROJ_NOT_WRITE_PERMISSION"},
        {0x32620136, "SET_LED_LASER"},
        {0x32620137, "LOADING_INTR_LASER_PARAMS"},
        {0x32620138, "SET_IR_MODE"},
        {0x32620140, "AI_REPLACE_RGB_FEATS_NOT_OBVIOUS"},
        {0x32620141, "IMPORTED_MESH_SIZE_NOT_SUPPORT"},
        {0x32620142, "MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION"},
        {0x32620143, "ALLOCATE_GPU_RESOURCES_FAILED"},
    };
    for (const auto& e : kNames)
        if (e.value == s) return e.name;
    return "UNKNOWN";
}

// Enumerations (32-bit): these ones are 0, 1, 2... (checked in the metadata).
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

// Messages from the library. Every listener is a structure holding one
// callback that takes the message by value (16 bytes: by address on x64);
// the library keeps the listener's address. It needs a device listener to
// fill its scanner list ("device callback is nullptr") and a data listener
// to preview ("data listener not found in preview"). IDs as coded in the
// metadata: the first of each list is 0, the rest 0xNN62xxxx.
enum DeviceMessageId : std::int32_t {
    kDeviceConnect = 0,
    kDeviceDisconnect = 0x12620100,
    kDeviceConnectNeedCalibration = 0x12620114,
    kDeviceConnectWithProblem = 0x12620116,
    kDeviceConnectReadParamsFailed = 0x12620117,
    kDeviceConnectCrcCheckFailed = 0x12620118,
};
enum DataMessageId : std::int32_t {  // data: an image (release it) or a frame (release it)
    kNewRgbImage = 0,
    kNewDepthImage = 0x13620100,
    kNewIrImage = 0x13620101,
    kFrameReady = 0x13620102,
};
enum ProcessMessageId : std::int32_t {
    kFrameBufferFull = 0x14620100,
    kMaxFramesReached = 0x14620101,
    kDetectFace = 0x14620102,
    kScanningFailed = 0x14620103,
    kNeedChangeVoxelSize = 0x14620104,
    kMarkersTooRegular = 0x14620105,
};
struct Message {
    std::int32_t msg_id;
    void* data;
};
using MessageCallback = void (*)(Message message);
struct MessageListener {
    MessageCallback onMessage;
};
// Kept for the code that names them so.
using DeviceMessage = Message;
using DeviceMessageListener = MessageListener;

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
using FnDeviceMsgListener = Status (*)(void* devices, MessageListener* listener);
using FnContextMsgListener = Status (*)(void* context, MessageListener* listener);
using FnRelease = Status (*)(void* handle);
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
    "obscan_context_add_data_msg_listener",
    "obscan_context_remove_data_msg_listener",
    "obscan_context_add_process_msg_listener",
    "obscan_context_remove_process_msg_listener",
    "obscan_image_release",
    "obscan_frame_release",
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
