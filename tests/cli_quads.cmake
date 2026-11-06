# SPDX-License-Identifier: GPL-3.0-or-later
# openrevers-cli run: box -> uniform remesh -> quads -> .tsm. Checks that
# standard output holds only the commands' JSON (QuadriFlow's progress goes
# to standard error) and that the T-Spline file is written.
file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
file(WRITE ${WORK}/box.obj "v 0 0 0\nv 20 0 0\nv 0 20 0\nv 20 20 0\nv 0 0 20\nv 20 0 20\nv 0 20 20\nv 20 20 20\n"
  "f 1 3 4 2\nf 5 6 8 7\nf 1 2 6 5\nf 3 7 8 4\nf 1 5 7 3\nf 2 4 8 6\n")
execute_process(
  COMMAND ${CLI} run
    mesh.import "{\"path\":\"${WORK}/box.obj\"}"
    mesh.remesh "{\"body\":\"b1\",\"edgeLength\":1.5,\"iterations\":5}"
    mesh.remesh_quads "{\"body\":\"b1\",\"targetFaces\":400}"
    mesh.export_tsm "{\"body\":\"b1\",\"path\":\"${WORK}/box.tsm\"}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "openrevers-cli failed (${rc}):\n${err}")
endif()
string(REGEX REPLACE "}[ \n]*{" "},{" joined "${out}")
string(JSON n ERROR_VARIABLE jerr LENGTH "[${joined}]")
if(jerr OR NOT n EQUAL 4)
  message(FATAL_ERROR "standard output is not just the four results (${jerr}):\n${out}")
endif()
string(JSON quads GET "[${joined}]" 2 result quads)
string(JSON faces GET "[${joined}]" 3 result faces)
if(quads LESS 200 OR NOT quads EQUAL faces)
  message(FATAL_ERROR "unexpected quads ${quads} / T-Spline faces ${faces}")
endif()
file(STRINGS ${WORK}/box.tsm first LIMIT_COUNT 1)
if(NOT first STREQUAL "#TS0200")
  message(FATAL_ERROR "box.tsm does not start with #TS0200")
endif()
message(STATUS "quads ${quads}, T-Spline faces ${faces}")
