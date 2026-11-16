# SPDX-License-Identifier: GPL-3.0-or-later
# OpenRevers — ingeniería inversa de mallas escaneadas / reverse engineering of scanned meshes.
cmake_minimum_required(VERSION 3.24)

project(OpenRevers
  VERSION 0.14.2
  DESCRIPTION "Reverse engineering of scanned meshes into parametric solids"
  HOMEPAGE_URL "https://github.com/MakerManiac3d/OpenRevers"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ORV_BUILD_APP   "Build the desktop application (needs Qt 6)" ON)
option(ORV_BUILD_TESTS "Build the unit tests" ON)
option(ORV_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in OpenRevers code" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(Dependencies)
include(Warnings)

add_subdirectory(src)

if(ORV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(Packaging)
