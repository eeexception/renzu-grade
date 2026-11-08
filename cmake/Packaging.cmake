# SPDX-License-Identifier: GPL-3.0-only
# Windows/Linux archives; macOS keeps its signature-aware package.sh workflow.
if(WIN32)
 set(RENZU_PACKAGE_PLATFORM windows)
 set(CPACK_GENERATOR ZIP)
 set(RENZU_INSTALL_INSTRUCTIONS [=[
Close DaVinci Resolve. Extract the archive, then copy the entire
RenzuGrade.ofx.bundle directory into:
  C:\Program Files\Common Files\OFX\Plugins\
Administrator permission may be required. Move any previous RenzuGrade.ofx.bundle
outside Plugins before replacing it. Restart Resolve, open Effects, and search
for Renzu Grade under Color Grading.

Uninstall: close Resolve and move only RenzuGrade.ofx.bundle outside Plugins.
The reference library in %LOCALAPPDATA%\Renzu Grade\References is preserved.
See docs/build-windows.md for source builds and troubleshooting.
]=])
else()
 set(RENZU_PACKAGE_PLATFORM linux)
 set(CPACK_GENERATOR TGZ)
 set(RENZU_INSTALL_INSTRUCTIONS [=[
Close DaVinci Resolve. Extract the archive, then copy the entire
RenzuGrade.ofx.bundle directory into:
  /usr/OFX/Plugins/
Administrator permission may be required. Move any previous RenzuGrade.ofx.bundle
outside Plugins before replacing it. Restart Resolve, open Effects, and search
for Renzu Grade under Color Grading.

Uninstall: close Resolve and move only RenzuGrade.ofx.bundle outside Plugins.
The reference library in $XDG_DATA_HOME/renzu-grade/references (default:
~/.local/share/renzu-grade/references) is preserved.
See docs/build-linux.md for runtime requirements and source builds.
]=])
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/INSTALL.txt.in"
 "${CMAKE_BINARY_DIR}/INSTALL.txt" @ONLY)
install(DIRECTORY "${BUNDLE}/" DESTINATION RenzuGrade.ofx.bundle USE_SOURCE_PERMISSIONS)
install(FILES LICENSE NOTICE README.md CONTRIBUTING.md CHANGELOG.md
 "${CMAKE_BINARY_DIR}/INSTALL.txt" DESTINATION .)
install(FILES third_party/ofx/LICENSE.md DESTINATION . RENAME OPENFX-LICENSE.md)
install(DIRECTORY docs DESTINATION .)
# Include the working source and all build scripts, including uncommitted local
# changes, without depending on .git or shipping build trees/private files.
install(DIRECTORY cmake include src shaders tests scripts docs .github
 DESTINATION Source/renzu-grade USE_SOURCE_PERMISSIONS
 PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
install(DIRECTORY third_party/ofx DESTINATION Source/renzu-grade/third_party)
install(FILES CMakeLists.txt LICENSE NOTICE README.md CONTRIBUTING.md CHANGELOG.md
 .gitignore .clang-format DESTINATION Source/renzu-grade)
set(CPACK_PACKAGE_NAME renzu-grade)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "renzu-grade-${PROJECT_VERSION}-${RENZU_PACKAGE_PLATFORM}-x86_64-cpu-only")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_VERBATIM_VARIABLES YES)
include(CPack)
