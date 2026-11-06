# SPDX-License-Identifier: GPL-3.0-or-later
# Copies QuadriFlow's sources and applies OpenRevers' changes (see QuadriFlow.cmake).
# Each change is checked: if upstream text moves, configuring fails loudly.

function(_orv_replace file from to)
  file(READ ${file} text)
  string(FIND "${text}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "QuadriFlow patch: text not found in ${file}:\n${from}")
  endif()
  string(REPLACE "${from}" "${to}" text "${text}")
  file(WRITE ${file} "${text}")
endfunction()

function(orv_patch_quadriflow src dst)
  # Patched in a staging folder, then copied only where a file changed, so
  # that configuring again does not rebuild QuadriFlow.
  set(stage ${dst}/.stage)
  file(REMOVE_RECURSE ${stage})
  file(COPY ${src}/src DESTINATION ${stage})
  set(S ${stage}/src)

  # 1. No Boost: drop its headers and replace the Boykov–Kolmogorov helper.
  _orv_replace(${S}/flow.hpp "#include <boost/graph/adjacency_list.hpp>\n" "")
  _orv_replace(${S}/flow.hpp "#include <boost/graph/boykov_kolmogorov_max_flow.hpp>\n" "")
  _orv_replace(${S}/flow.hpp "#include <boost/graph/edmonds_karp_max_flow.hpp>\n" "")
  _orv_replace(${S}/flow.hpp "#include <boost/graph/push_relabel_max_flow.hpp>\n" "")
  _orv_replace(${S}/flow.hpp "using namespace boost;\n" "")
  # QuadriFlow's own augmenting-path solver (ECMaxFlowHelper, Edmonds–Karp)
  # takes the place of Boost's Boykov–Kolmogorov. A push-relabel flow was
  # tried and rejected: its flows take long detours, and the quad count fell
  # far below the target. Edmonds–Karp gives the same quads as Boost, at
  # 1.1–1.6× the time.
  file(READ ${S}/flow.hpp text)
  string(FIND "${text}" "class BoykovMaxFlowHelper" a)
  string(FIND "${text}" "class NetworkSimplexFlowHelper" b)
  if(a EQUAL -1 OR b EQUAL -1)
    message(FATAL_ERROR "QuadriFlow patch: flow helpers not found in flow.hpp")
  endif()
  string(SUBSTRING "${text}" 0 ${a} head)
  string(SUBSTRING "${text}" ${b} -1 tail)
  file(WRITE ${S}/flow.hpp "${head}${tail}")
  _orv_replace(${S}/optimizer.cpp "solver = std::make_unique<BoykovMaxFlowHelper>();" "solver = std::make_unique<ECMaxFlowHelper>();")

  # 2. Never start another program: the SAT step's system() calls are removed.
  _orv_replace(${S}/hierarchy.cpp "system(\"which minisat > /dev/null 2>&1\")" "1")
  _orv_replace(${S}/hierarchy.cpp "system(\"which timeout > /dev/null 2>&1\")" "1")
  file(READ ${S}/localsat.cpp text)
  string(REPLACE "int exit_code = system(cmd);" "int exit_code = -1; (void)cmd;" text "${text}")
  string(REPLACE "system(cmd);" "(void)cmd;" text "${text}")
  string(FIND "${text}" "system(" left)
  if(NOT left EQUAL -1)
    message(FATAL_ERROR "QuadriFlow patch: a system() call is left in localsat.cpp")
  endif()
  file(WRITE ${S}/localsat.cpp "${text}")

  file(MAKE_DIRECTORY ${dst}/src)
  file(GLOB staged RELATIVE ${S} ${S}/*)
  foreach(f IN LISTS staged)
    file(COPY_FILE ${S}/${f} ${dst}/src/${f} ONLY_IF_DIFFERENT)
  endforeach()
  file(REMOVE_RECURSE ${stage})
endfunction()
