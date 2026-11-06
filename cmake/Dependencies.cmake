# SPDX-License-Identifier: GPL-3.0-or-later
# Third-party libraries. Each one is listed, with its licence and use, in THIRD_PARTY.md.
# A library found on the system is used; otherwise it is fetched at a pinned version.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# --- Eigen 3.4 (MPL-2.0): linear algebra, also needed by PMP ---------------
find_package(Eigen3 3.4 QUIET NO_MODULE)
if(NOT TARGET Eigen3::Eigen)
  FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    FALSE)
  set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(eigen)
endif()

# --- nlohmann/json 3.11 (MIT): commands, project files, MCP ----------------
find_package(nlohmann_json 3.11 QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  FetchContent_MakeAvailable(json)
endif()

# --- PMP Library 3.0.0 (MIT): decimation, remeshing, hole filling, smoothing -
FetchContent_Declare(pmp
  GIT_REPOSITORY https://github.com/pmp-library/pmp-library.git
  GIT_TAG        f2fb04f4a4188a5c1ab137e83b96e62fa99c639f # 3.0.0
  GIT_SHALLOW    FALSE)
set(PMP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_VIS OFF CACHE BOOL "" FORCE)
set(PMP_INSTALL OFF CACHE BOOL "" FORCE)
set(PMP_STRICT_COMPILATION OFF CACHE BOOL "" FORCE)
set(_orv_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pmp)
set(BUILD_SHARED_LIBS ${_orv_shared} CACHE BOOL "" FORCE)
# PMP adds its headers with a directory-wide include; give its target one of its own.
target_include_directories(pmp SYSTEM INTERFACE $<BUILD_INTERFACE:${pmp_SOURCE_DIR}/src>)
# PMP sets its own output folders; keep ours.
set(LIBRARY_OUTPUT_PATH "")
set(EXECUTABLE_OUTPUT_PATH "")

# --- GoogleTest 1.14 (BSD-3): unit tests ------------------------------------
if(ORV_BUILD_TESTS)
  find_package(GTest 1.14 QUIET)
  if(NOT TARGET GTest::gtest_main)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
      GIT_SHALLOW    FALSE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
endif()

# --- Qt 6 (LGPL-3.0): the desktop application -------------------------------
if(ORV_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Concurrent LinguistTools)
  qt_standard_project_setup()
endif()
