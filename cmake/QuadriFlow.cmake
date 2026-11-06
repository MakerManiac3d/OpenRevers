# SPDX-License-Identifier: GPL-3.0-or-later
# QuadriFlow (BSD-3-Clause): quad remeshing.
#   J. Huang, Y. Zhou, M. Niessner, J. R. Shewchuk, L. J. Guibas,
#   "QuadriFlow: A Scalable and Robust Method for Quadrangulation", SGP 2018.
#
# Built from its sources at a pinned commit, patched by
# cmake/PatchQuadriFlow.cmake so that it
#   - needs no Boost: its Boykov–Kolmogorov max-flow helper is replaced by
#     QuadriFlow's own Edmonds–Karp helper (the same quads, a little slower);
#   - never starts another program: the optional SAT step calls `minisat`
#     through system(); those calls are removed (the step stays off).
# LEMON 1.3.1 (Boost Software License), bundled with QuadriFlow, is used
# header-only (LEMON_ONLY_TEMPLATES) with a minimal generated config.h.
include(FetchContent)
FetchContent_Declare(quadriflow
  GIT_REPOSITORY https://github.com/hjwdzh/QuadriFlow.git
  GIT_TAG        810b7a0967c35b0dc85b4464e3835e26a756c967
  GIT_SHALLOW    FALSE
  SOURCE_SUBDIR  _not_a_cmake_project_here)
FetchContent_MakeAvailable(quadriflow)

set(QF ${quadriflow_SOURCE_DIR})
set(QF_PATCHED ${CMAKE_BINARY_DIR}/quadriflow-patched)
include(${CMAKE_CURRENT_LIST_DIR}/PatchQuadriFlow.cmake)
orv_patch_quadriflow(${QF} ${QF_PATCHED})

file(WRITE ${QF_PATCHED}/lemon-config/lemon/config.h
  "#define LEMON_VERSION \"1.3.1\"\n#define LEMON_HAVE_LONG_LONG 1\n")

add_library(quadriflow STATIC
  ${QF_PATCHED}/src/adjacent-matrix.cpp
  ${QF_PATCHED}/src/dedge.cpp
  ${QF_PATCHED}/src/hierarchy.cpp
  ${QF_PATCHED}/src/loader.cpp
  ${QF_PATCHED}/src/localsat.cpp
  ${QF_PATCHED}/src/merge-vertex.cpp
  ${QF_PATCHED}/src/optimizer.cpp
  ${QF_PATCHED}/src/parametrizer.cpp
  ${QF_PATCHED}/src/parametrizer-flip.cpp
  ${QF_PATCHED}/src/parametrizer-int.cpp
  ${QF_PATCHED}/src/parametrizer-mesh.cpp
  ${QF_PATCHED}/src/parametrizer-scale.cpp
  ${QF_PATCHED}/src/parametrizer-sing.cpp
  ${QF_PATCHED}/src/subdivide.cpp)
target_include_directories(quadriflow SYSTEM PUBLIC
  ${QF_PATCHED}/src
  ${QF}/3rd/pcg32
  ${QF}/3rd/lemon-1.3.1
  ${QF_PATCHED}/lemon-config)
target_compile_definitions(quadriflow PUBLIC LEMON_ONLY_TEMPLATES EIGEN_MPL2_ONLY)
target_link_libraries(quadriflow PUBLIC Eigen3::Eigen)
set_target_properties(quadriflow PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_definitions(quadriflow PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_compile_options(quadriflow PRIVATE /W0 /bigobj)
else()
  target_compile_options(quadriflow PRIVATE -w)
endif()
