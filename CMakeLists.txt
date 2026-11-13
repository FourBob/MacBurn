cmake_minimum_required(VERSION 3.24)

set(CMAKE_OSX_DEPLOYMENT_TARGET "27.0" CACHE STRING "Mindestversion von macOS")
project(MacBurn VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Fremdbibliotheken (unverändert, siehe third_party/*/HERKUNFT.md)
# ---------------------------------------------------------------------------
add_library(clipper2 STATIC
  third_party/clipper2/src/clipper.engine.cpp
  third_party/clipper2/src/clipper.offset.cpp
  third_party/clipper2/src/clipper.rectclip.cpp
)
target_include_directories(clipper2 SYSTEM PUBLIC third_party/clipper2/include)
target_compile_options(clipper2 PRIVATE -w)

# ---------------------------------------------------------------------------
# Kern: portables C++, keine Objective-C-Abhängigkeit
# ---------------------------------------------------------------------------
add_library(macburn_core STATIC
  src/core/util/Json.cpp
  src/core/profiles/Profiles.cpp
  src/core/geometry/Geometry.cpp
  src/core/geometry/Boolean.cpp
  src/core/document/Document.cpp
  src/core/job/Job.cpp
  src/core/device/Grbl.cpp
  src/core/device/GrblDevice.cpp
  src/core/render/RenderList.cpp
  src/core/app/AppController.cpp
  src/core/app/AppDevice.cpp
  src/core/app/AppEdit.cpp
  src/core/io/Xml.cpp
  src/core/io/SvgImport.cpp
  src/core/io/Project.cpp
)
target_include_directories(macburn_core PUBLIC src/core)
target_link_libraries(macburn_core PRIVATE clipper2)
target_compile_options(macburn_core PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

# ---------------------------------------------------------------------------
# App: dünne Cocoa-Schicht
# ---------------------------------------------------------------------------
add_executable(MacBurn MACOSX_BUNDLE
  src/mac/main.mm
  src/mac/AppDelegate.mm
  src/mac/MainWindow.mm
  src/mac/CanvasView.mm
  src/mac/InspectorView.mm
)
target_include_directories(MacBurn PRIVATE src/mac)
target_compile_options(MacBurn PRIVATE -fobjc-arc -Wall)
target_link_libraries(MacBurn PRIVATE macburn_core
  "-framework AppKit" "-framework UniformTypeIdentifiers" "-framework QuartzCore")
set_target_properties(MacBurn PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
  MACOSX_BUNDLE_BUNDLE_NAME "MacBurn"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Mitgelieferte Systemprofile ins Bundle kopieren
add_custom_command(TARGET MacBurn POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/profiles"
          "$<TARGET_BUNDLE_CONTENT_DIR:MacBurn>/Resources/profiles"
  COMMENT "Profile ins App-Bundle kopieren")

# ---------------------------------------------------------------------------
# Werkzeug: liest $I und $$ von einem GRBL-Gerät
# ---------------------------------------------------------------------------
add_executable(mbprobe tools/mbprobe.cpp)
target_link_libraries(mbprobe PRIVATE macburn_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(macburn_tests tests/tests.cpp)
target_link_libraries(macburn_tests PRIVATE macburn_core)
target_compile_definitions(macburn_tests PRIVATE MB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME core COMMAND macburn_tests)
