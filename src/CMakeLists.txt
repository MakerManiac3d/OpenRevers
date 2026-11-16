# SPDX-License-Identifier: GPL-3.0-or-later
configure_file(core/util/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/util/Version.h @ONLY)

# PoissonRecon's templates, compiled once and on their own (C++17, no warnings).
add_library(orv_poisson STATIC core/points/PoissonBridge.cpp)
target_include_directories(orv_poisson PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} SYSTEM PRIVATE ${poissonrecon_SOURCE_DIR}/Src)
set_target_properties(orv_poisson PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(orv_poisson PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(orv_poisson PRIVATE /W0 /bigobj /utf-8)
  target_compile_definitions(orv_poisson PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  target_compile_options(orv_poisson PRIVATE -w)
endif()

# The core: geometry, document, commands and the MCP protocol. No interface,
# no Qt — used by the app, the command line, the agent bridge and the tests.
# PlaneGCS (FreeCAD's sketch constraint solver, LGPL-2.1+): its own target,
# without our warnings (third-party code).
add_library(orv_planegcs STATIC
  ${PROJECT_SOURCE_DIR}/thirdparty/planegcs/Constraints.cpp
  ${PROJECT_SOURCE_DIR}/thirdparty/planegcs/GCS.cpp
  ${PROJECT_SOURCE_DIR}/thirdparty/planegcs/Geo.cpp
  ${PROJECT_SOURCE_DIR}/thirdparty/planegcs/SubSystem.cpp
  ${PROJECT_SOURCE_DIR}/thirdparty/planegcs/qp_eq.cpp)
target_include_directories(orv_planegcs SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/thirdparty/planegcs ${PROJECT_SOURCE_DIR}/thirdparty/planegcs/shims)
target_link_libraries(orv_planegcs PUBLIC Eigen3::Eigen)
set_target_properties(orv_planegcs PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(orv_planegcs PRIVATE /W0 /bigobj)
else()
  target_compile_options(orv_planegcs PRIVATE -w)
endif()

add_library(orv_core STATIC
  core/mesh/TriMesh.cpp
  core/mesh/MeshIO.cpp
  core/mesh/MeshAnalysis.cpp
  core/mesh/MeshRepair.cpp
  core/mesh/PmpBridge.cpp
  core/mesh/MeshQuad.cpp
  core/mesh/ThreeMf.cpp
  core/points/PointCloud.cpp
  core/fit/Primitive.cpp
  core/fit/Segmentation.cpp
  core/fit/Align.cpp
  core/fit/Register.cpp
  core/mesh/MeshClean.cpp
  core/solid/BSpline.cpp
  core/solid/Drawing.cpp
  core/solid/Profile.cpp
  core/solid/Solid.cpp
  core/solid/SketchFit.cpp
  core/solid/SketchSolve.cpp
  core/mesh/Distance.cpp
  core/mesh/DeviationReport.cpp
  core/mesh/MeshProbe.cpp
  core/points/E57.cpp
  core/util/Xml.cpp
  core/util/Config.cpp
  doc/Document.cpp
  doc/Project.cpp
  doc/Model.cpp
  commands/Command.cpp
  commands/CoreCommands.cpp
  commands/FeatureCommands.cpp
  commands/ProbeCommands.cpp
  commands/PartCommands.cpp
  commands/ScanCommands.cpp
  commands/FieldKind.cpp
  scan/ScanBackend.cpp
  scan/ObScanBackend.cpp
  commands/Guide.cpp
  mcp/McpServer.cpp
  mcp/LocalBackend.cpp
  mcp/AgentLinkClient.cpp)
target_include_directories(orv_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(orv_core PUBLIC Eigen3::Eigen nlohmann_json::nlohmann_json PRIVATE orv_planegcs pmp quadriflow orv_miniz orv_nanoflann orv_poisson orv_occt)
if(WIN32)
  target_link_libraries(orv_core PRIVATE ws2_32)
endif()
orv_set_warnings(orv_core)

add_executable(openrevers-cli cli/main.cpp)
target_link_libraries(openrevers-cli PRIVATE orv_core)
orv_set_warnings(openrevers-cli)
install(TARGETS openrevers-cli RUNTIME DESTINATION bin)

if(ORV_BUILD_APP)
  add_subdirectory(app)
endif()
