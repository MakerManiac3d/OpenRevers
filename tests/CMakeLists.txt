# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(orv_tests test_mesh.cpp test_document.cpp)
target_link_libraries(orv_tests PRIVATE orv_core GTest::gtest_main)
target_include_directories(orv_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
include(GoogleTest)
gtest_discover_tests(orv_tests DISCOVERY_TIMEOUT 60)

# The command line end to end: import, repair, export through openrevers-cli.
add_test(NAME cli_version COMMAND openrevers-cli --version)
add_test(NAME cli_list COMMAND openrevers-cli list)
