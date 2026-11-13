# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(OpenRevers WIN32 MACOSX_BUNDLE
  main.cpp
  MainWindow.cpp MainWindow.h
  Viewport.cpp Viewport.h
  CommandRunner.cpp CommandRunner.h
  CommandPanel.cpp CommandPanel.h
  Icons.cpp Icons.h
  Ribbon.cpp Ribbon.h
  AgentServer.cpp AgentServer.h
  AppCommands.cpp AppCommands.h
  SettingsDialog.cpp SettingsDialog.h
  SketchEditor.cpp SketchEditor.h
  PairAlignDialog.cpp PairAlignDialog.h)
target_link_libraries(OpenRevers PRIVATE orv_core
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network Qt6::Concurrent)
target_include_directories(OpenRevers PRIVATE ${PROJECT_SOURCE_DIR}/src)
orv_set_warnings(OpenRevers)

qt_add_resources(OpenRevers icons PREFIX "/" BASE ${PROJECT_SOURCE_DIR}/packaging FILES ${PROJECT_SOURCE_DIR}/packaging/openrevers.svg)

# Translations: sources are in English; Spanish in translations/openrevers_es.ts.
qt_add_translations(OpenRevers
  TS_FILES ${PROJECT_SOURCE_DIR}/translations/openrevers_es.ts
  RESOURCE_PREFIX "/i18n"
  LUPDATE_OPTIONS -no-obsolete)

if(WIN32)
  target_sources(OpenRevers PRIVATE ${PROJECT_SOURCE_DIR}/packaging/openrevers.rc)
endif()
set_target_properties(OpenRevers PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.makermaniac3d.openrevers
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})

install(TARGETS OpenRevers RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(WIN32)
  # Qt's DLLs, plugins and translations beside the executable, for the installer.
  qt_generate_deploy_app_script(TARGET OpenRevers OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${deploy_script})
endif()
