# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(orv_tests test_mesh.cpp test_document.cpp test_quads.cpp test_3mf.cpp test_points.cpp test_e57.cpp test_fit.cpp test_segment.cpp test_align.cpp test_solid.cpp test_scan.cpp test_clean_align.cpp test_parity.cpp test_preview.cpp test_sketch_solve.cpp)
target_link_libraries(orv_tests PRIVATE orv_core orv_miniz GTest::gtest_main)
target_include_directories(orv_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(orv_tests PRIVATE ORV_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
include(GoogleTest)
gtest_discover_tests(orv_tests DISCOVERY_TIMEOUT 60)

# The command line end to end: import, repair, export through openrevers-cli.
add_test(NAME cli_version COMMAND openrevers-cli --version)
add_test(NAME cli_list COMMAND openrevers-cli list)

# Quad remesh and T-Spline export from the command line; the JSON on stdout
# must stay clean although QuadriFlow prints its progress.
add_test(NAME cli_quads_tsm
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:openrevers-cli> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_quads
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_quads.cmake)

# Large-mesh benchmark, run by hand: see docs/desarrollo/rendimiento.md.
add_executable(orv_bench bench_large.cpp)
target_link_libraries(orv_bench PRIVATE orv_core)
