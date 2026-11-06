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

# --- miniz 3.0.2 (MIT): ZIP for 3MF packages ---------------------------------
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR _not_a_cmake_project_here)
FetchContent_MakeAvailable(miniz)
add_library(orv_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(orv_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
# Only memory archives are used: no file or time functions.
target_compile_definitions(orv_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME)
set_target_properties(orv_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(orv_miniz PRIVATE /W0)
  target_compile_definitions(orv_miniz PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(orv_miniz PRIVATE -w)
endif()

# --- nanoflann 1.6.1 (BSD-2): nearest neighbours in point clouds -------------
FetchContent_Declare(nanoflann
  GIT_REPOSITORY https://github.com/jlblancoc/nanoflann.git
  GIT_TAG        d2fdfed6bcb940a07d8b217ed589f1ff89aa7324 # v1.6.1
  GIT_SHALLOW    FALSE
  SOURCE_SUBDIR  _not_a_cmake_project_here)
FetchContent_MakeAvailable(nanoflann)
add_library(orv_nanoflann INTERFACE)
target_include_directories(orv_nanoflann SYSTEM INTERFACE ${nanoflann_SOURCE_DIR}/include)

# --- PoissonRecon 18.76 (MIT): screened Poisson surface reconstruction --------
# Header-only use of its library interface (Src/Reconstructors.h), compiled in
# one file of our own (src/core/points/PoissonBridge.cpp) as C++17.
FetchContent_Declare(poissonrecon
  GIT_REPOSITORY https://github.com/mkazhdan/PoissonRecon.git
  GIT_TAG        262b0f539d404057d1f36e1adc07fc9388678899 # 18.76
  GIT_SHALLOW    FALSE
  SOURCE_SUBDIR  _not_a_cmake_project_here)
FetchContent_MakeAvailable(poissonrecon)

# --- QuadriFlow (BSD-3) with LEMON (Boost licence): quad remeshing ------------
include(QuadriFlow)

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
