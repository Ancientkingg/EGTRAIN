cmake_minimum_required(VERSION 3.16)
set(EGTRAIN_VERSION "1.0.0" CACHE STRING "Application version for this build")
if(NOT EGTRAIN_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "EGTRAIN_VERSION must be a stable X.Y.Z version")
endif()
project(EGTRAIN VERSION ${EGTRAIN_VERSION} LANGUAGES C CXX)

# The application and the Qt and vcpkg packages it is built with are 64-bit only on Windows.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "EGTRAIN supports 64-bit Windows (x64) only. Configure for x64: "
        "-A x64 with a Visual Studio generator, or an x64 configuration in Visual Studio.")
endif()

# The project version is the single application-version source for every
# target and package surface.
add_compile_definitions(EGTRAIN_APP_VERSION=\"${PROJECT_VERSION}\")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/EGTRAIN_VERSION" CONTENT "${PROJECT_VERSION}\n")

if(WIN32)
    enable_language(RC)
endif()

# Default to an optimised build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# clang-tidy and editors read build/compile_commands.json (Makefile and Ninja generators only).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# MSVC's <cmath> only defines M_PI and friends when _USE_MATH_DEFINES is set
# before the header is included. Harmless on other toolchains.
add_compile_definitions(_USE_MATH_DEFINES)

# On Windows, <zmq.hpp> drags in <windows.h>. Tame it so the SDK does not
# collide with the project: WIN32_LEAN_AND_MEAN trims the include (and drops the
# legacy winsock.h that fights ZeroMQ's winsock2.h), NOMINMAX drops the min/max
# macros, NOGDI drops the GDI Arc() that clashes with our class Arc, and
# _HAS_STD_BYTE=0 resolves the std::byte vs SDK byte ambiguity (std::byte unused).
if(MSVC)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX NOGDI _HAS_STD_BYTE=0)
    # MainWindow.cpp exceeds the section limit of a standard object file in RelWithDebInfo builds.
    add_compile_options(/bigobj)
    # Qt 5.15 headers need a macro override to compile with Visual Studio 2026.
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/EGTRAIN/QEGTRAIN/util/QtMsvcCompat.h>")
endif()

option(EGTRAIN_ENABLE_SANITIZERS "Enable AddressSanitizer/UndefinedBehaviorSanitizer for local debug checks" OFF)
option(EGTRAIN_PACKAGED_BUILD "Enable self-update support for a release package" OFF)
option(EGTRAIN_ENABLE_TELEMETRY "Enable consent UI for packaged interactive builds" OFF)
option(EGTRAIN_ISOLATED_TELEMETRY_SMOKE "Enable isolated numeric-loopback application smoke hook" OFF)
if(EGTRAIN_ISOLATED_TELEMETRY_SMOKE AND (NOT EGTRAIN_BUILD_TESTS OR NOT EGTRAIN_PACKAGED_BUILD))
    message(FATAL_ERROR "Isolated telemetry smoke requires EGTRAIN_BUILD_TESTS and EGTRAIN_PACKAGED_BUILD")
endif()
set(EGTRAIN_TELEMETRY_URL "" CACHE STRING "Exact compiled HTTPS telemetry receiver URL")
if(EGTRAIN_TELEMETRY_URL MATCHES "[\"\\\\;]")
    message(FATAL_ERROR "EGTRAIN_TELEMETRY_URL contains unsupported characters")
endif()
if(EGTRAIN_ENABLE_TELEMETRY)
    add_compile_definitions(EGTRAIN_ENABLE_TELEMETRY=1)
else()
    add_compile_definitions(EGTRAIN_ENABLE_TELEMETRY=0)
endif()
add_compile_definitions(EGTRAIN_TELEMETRY_URL="${EGTRAIN_TELEMETRY_URL}")

if(EGTRAIN_PACKAGED_BUILD)
    add_compile_definitions(EGTRAIN_PACKAGED_BUILD=1)
else()
    add_compile_definitions(EGTRAIN_PACKAGED_BUILD=0)
endif()

if(EGTRAIN_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(WARNING "EGTRAIN_ENABLE_SANITIZERS is only configured for Clang/GNU")
    endif()
endif()

# Enable link-time optimisation for Release builds (38 translation units,
# compute-heavy simulation core - cross-TU inlining gives a real speedup).
include(CheckIPOSupported)
check_ipo_supported(RESULT EGTRAIN_IPO_SUPPORTED OUTPUT EGTRAIN_IPO_ERROR)

# Sources are in the QEGTRAIN subdirectory
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/EGTRAIN/QEGTRAIN)

add_library(egtrain_miniz STATIC
    ${SRC_DIR}/io/third_party/miniz/miniz.c)
target_compile_definitions(egtrain_miniz PUBLIC MINIZ_NO_TIME)
target_include_directories(egtrain_miniz PUBLIC ${SRC_DIR})
set_target_properties(egtrain_miniz PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Path-qualified includes (e.g. graphics/items/NodeItem.h) resolve against SRC_DIR
include_directories(${SRC_DIR})

# --- Dependencies ---
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Charts Svg Network)

# --- ZeroMQ / nlohmann_json ---
find_package(cppzmq REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Model libraries ---
# Each source below is compiled once. The libraries are static, so a target that links one gets
# only the objects it references. A target that pulls in Infrastructure,
# Signalling or RollingStock from egtrain_sim must define `Logger owl`.
add_library(egtrain_pugixml STATIC ${SRC_DIR}/io/third_party/pugixml.cpp)

add_library(egtrain_util STATIC
    ${SRC_DIR}/util/CsvWriter.cpp
    ${SRC_DIR}/util/Logger.cpp
    ${SRC_DIR}/util/PlaybackProfiler.cpp
    ${SRC_DIR}/util/SpeedFormat.cpp
    ${SRC_DIR}/util/TimeFormat.cpp
    ${SRC_DIR}/util/TrajectoryUtil.cpp
    ${SRC_DIR}/util/Version.cpp)
target_link_libraries(egtrain_util PRIVATE nlohmann_json::nlohmann_json)

# No Qt: scene_tool links this library.
add_library(egtrain_scene STATIC
    ${SRC_DIR}/scene/SceneBundle.cpp
    ${SRC_DIR}/scene/SceneCompatibility.cpp
    ${SRC_DIR}/scene/SceneDiagnostic.cpp
    ${SRC_DIR}/scene/SceneExporter.cpp
    ${SRC_DIR}/scene/SceneImporter.cpp
    ${SRC_DIR}/scene/SceneMigration.cpp
    ${SRC_DIR}/scene/SceneModel.cpp
    ${SRC_DIR}/scene/SceneValidator.cpp
    ${SRC_DIR}/scene/SceneWriter.cpp
    ${SRC_DIR}/scene/SectionInventory.cpp
    ${SRC_DIR}/scene/StopInsertion.cpp
    ${SRC_DIR}/scene/TrackPreview.cpp)
target_link_libraries(egtrain_scene
    PUBLIC nlohmann_json::nlohmann_json egtrain_miniz)

add_library(egtrain_sim STATIC
    ${SRC_DIR}/simulation/Infrastructure.cpp
    ${SRC_DIR}/simulation/InitialParameters.cpp
    ${SRC_DIR}/simulation/NumberGenerator.cpp
    ${SRC_DIR}/simulation/Optimisation.cpp
    ${SRC_DIR}/simulation/Passengers.cpp
    ${SRC_DIR}/simulation/RollingStock.cpp
    ${SRC_DIR}/simulation/Signalling.cpp
    ${SRC_DIR}/simulation/Simulation.cpp)
target_link_libraries(egtrain_sim
    PUBLIC egtrain_scene egtrain_util nlohmann_json::nlohmann_json)

# RailMLParser.cpp uses `initial_variables`, which egtrain_sim or the target itself defines.
# List egtrain_railml before egtrain_sim.
add_library(egtrain_railml STATIC ${SRC_DIR}/io/RailMLParser.cpp)
target_link_libraries(egtrain_railml
    PUBLIC egtrain_pugixml nlohmann_json::nlohmann_json
    PRIVATE cppzmq)

foreach(egtrain_lib egtrain_pugixml egtrain_util egtrain_scene egtrain_sim egtrain_railml)
    target_include_directories(${egtrain_lib} PUBLIC ${SRC_DIR})
    set_target_properties(${egtrain_lib} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
endforeach()

# --- Qt libraries ---
# Each source is compiled once, as for the model libraries. These libraries have Q_OBJECT
# classes, so AUTOMOC stays on. A target that uses one class of a library also gets the other
# classes of that library, because their moc code is in one object. None depends on MainWindow.
add_library(egtrain_graphics STATIC
    ${SRC_DIR}/graphics/NetworkScene.cpp
    ${SRC_DIR}/graphics/NetworkView.cpp
    ${SRC_DIR}/graphics/VisualPolish.cpp
    ${SRC_DIR}/graphics/items/ConnectionItem.cpp
    ${SRC_DIR}/graphics/items/HighlightEffect.cpp
    ${SRC_DIR}/graphics/items/IconItem.cpp
    ${SRC_DIR}/graphics/items/NodeItem.cpp
    ${SRC_DIR}/graphics/items/PassengerItem.cpp
    ${SRC_DIR}/graphics/items/PlatformItem.cpp
    ${SRC_DIR}/graphics/items/SignalItem.cpp
    ${SRC_DIR}/graphics/items/StationNodeItem.cpp
    ${SRC_DIR}/graphics/items/StationOverlayItem.cpp
    ${SRC_DIR}/graphics/items/TrackLineItem.cpp
    ${SRC_DIR}/graphics/items/TrainBadgeItem.cpp
    ${SRC_DIR}/graphics/items/TrainBodyItem.cpp
    ${SRC_DIR}/graphics/items/TrainItemGroup.cpp
    ${SRC_DIR}/graphics/items/VirtualArcItem.cpp)
target_link_libraries(egtrain_graphics
    PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_scene egtrain_util nlohmann_json::nlohmann_json)

add_library(egtrain_widgets STATIC
    ${SRC_DIR}/widgets/AboutDialog.cpp
    ${SRC_DIR}/widgets/ColorChoiceButton.cpp
    ${SRC_DIR}/widgets/ConsoleWidget.cpp
    ${SRC_DIR}/widgets/InfoDockWidget.cpp
    ${SRC_DIR}/widgets/NetworkLegendWidget.cpp
    ${SRC_DIR}/widgets/TimeProgressBar.cpp)
target_link_libraries(egtrain_widgets
    PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_graphics egtrain_util)

# The update code that both the application and a test use. UpdateChecker and SelfUpdater belong
# to the application alone.
add_library(egtrain_update STATIC
    ${SRC_DIR}/update/ReleaseInfo.cpp
    ${SRC_DIR}/update/UpdatePreparation.cpp
    ${SRC_DIR}/update/UpdateSettings.cpp)
target_link_libraries(egtrain_update PUBLIC Qt5::Core egtrain_util)

# The test hooks change the layout of TelemetrySender, TelemetryConsent and TelemetryQueue, so a
# build with hooks compiles the same sources again, by design:
#   egtrain_telemetry        no hooks: the application and the diagram window tests
#   egtrain_telemetry_hooks  all three hooks, for every telemetry test (tests only)
#   egtrain_telemetry_smoke  the sender hook: the application in the isolated smoke build
# A target links exactly one of them.
set(EGTRAIN_TELEMETRY_SOURCES
    ${SRC_DIR}/telemetry/TelemetryConsent.cpp
    ${SRC_DIR}/telemetry/TelemetryEvent.cpp
    ${SRC_DIR}/telemetry/TelemetryOperation.cpp
    ${SRC_DIR}/telemetry/TelemetryQueue.cpp
    ${SRC_DIR}/telemetry/TelemetrySender.cpp)
set(EGTRAIN_TELEMETRY_LIBS egtrain_telemetry)
add_library(egtrain_telemetry STATIC ${EGTRAIN_TELEMETRY_SOURCES})
if(EGTRAIN_BUILD_TESTS)
    add_library(egtrain_telemetry_hooks STATIC ${EGTRAIN_TELEMETRY_SOURCES})
    target_compile_definitions(egtrain_telemetry_hooks PUBLIC
        EGTRAIN_SENDER_TEST_HOOK=1 EGTRAIN_CONSENT_TEST_HOOK=1 EGTRAIN_TELEMETRY_TEST_HOOK=1)
    list(APPEND EGTRAIN_TELEMETRY_LIBS egtrain_telemetry_hooks)
endif()
if(EGTRAIN_ISOLATED_TELEMETRY_SMOKE)
    add_library(egtrain_telemetry_smoke STATIC ${EGTRAIN_TELEMETRY_SOURCES})
    target_compile_definitions(egtrain_telemetry_smoke PUBLIC EGTRAIN_SENDER_TEST_HOOK=1)
    list(APPEND EGTRAIN_TELEMETRY_LIBS egtrain_telemetry_smoke)
endif()
foreach(egtrain_lib ${EGTRAIN_TELEMETRY_LIBS})
    target_link_libraries(${egtrain_lib} PUBLIC Qt5::Core Qt5::Gui Qt5::Network Qt5::Widgets)
endforeach()

# The application, test_simulationworker and test_characterization run the simulation through these.
# They need `Logger owl` from the target, like egtrain_sim.
add_library(egtrain_dispatch STATIC
    ${SRC_DIR}/app/DispatchController.cpp
    ${SRC_DIR}/simulation/SimulationWorker.cpp)
target_link_libraries(egtrain_dispatch
    PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_railml egtrain_sim)

# DiagramWindow and TimetableTableWindow call telemetry::OperationObservation, whose symbols do
# not depend on the hooks, so this library is built without them in every configuration. The moc
# object of the library references both windows, so a target that uses TrainFilterButton,
# DiagramWindow or TimetableTableWindow also links one telemetry library.
add_library(egtrain_diagrams STATIC
    ${SRC_DIR}/diagrams/BlockingTimeDiagram.cpp
    ${SRC_DIR}/diagrams/CapacityAnalysis.cpp
    ${SRC_DIR}/diagrams/DiagramWindow.cpp
    ${SRC_DIR}/diagrams/RouteDiagramCoordinates.cpp
    ${SRC_DIR}/diagrams/RouteDiagramRuntime.cpp
    ${SRC_DIR}/diagrams/RouteReferenceChoice.cpp
    ${SRC_DIR}/diagrams/RunResults.cpp
    ${SRC_DIR}/diagrams/TimetableTableWindow.cpp
    ${SRC_DIR}/diagrams/TractionCurve.cpp
    ${SRC_DIR}/diagrams/TrainFilterButton.cpp)
target_link_libraries(egtrain_diagrams
    PUBLIC Qt5::Core Qt5::Gui Qt5::Network Qt5::Widgets Qt5::Charts egtrain_sim)

foreach(egtrain_lib egtrain_graphics egtrain_widgets egtrain_update ${EGTRAIN_TELEMETRY_LIBS}
        egtrain_dispatch egtrain_diagrams)
    target_include_directories(${egtrain_lib} PUBLIC ${SRC_DIR})
    set_target_properties(${egtrain_lib} PROPERTIES
        AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
endforeach()

# --- Sources ---
set(EGTRAIN_SOURCES
    ${SRC_DIR}/app/main.cpp
    ${SRC_DIR}/app/MainWindow.cpp
    ${SRC_DIR}/telemetry/TelemetryConsentDialog.cpp
    ${SRC_DIR}/update/SelfUpdater.cpp
    ${SRC_DIR}/update/UpdateChecker.cpp
)

set(EGTRAIN_HEADERS
    ${SRC_DIR}/widgets/DialogLayout.h
    ${SRC_DIR}/widgets/ChoiceComboBox.h
    ${SRC_DIR}/simulation/Infrastructure.h
    ${SRC_DIR}/simulation/InitialParameters.h
    ${SRC_DIR}/io/RailMLParser.h
    ${SRC_DIR}/app/MainWindow.h
    ${SRC_DIR}/simulation/NumberGenerator.h
    ${SRC_DIR}/simulation/Optimisation.h
    ${SRC_DIR}/util/Logger.hpp
    ${SRC_DIR}/util/PlaybackProfiler.h
    ${SRC_DIR}/simulation/Passengers.h
    ${SRC_DIR}/util/portability.h
    ${SRC_DIR}/simulation/RollingStock.h
    ${SRC_DIR}/simulation/Signalling.h
    ${SRC_DIR}/simulation/Simulation.h
    ${SRC_DIR}/util/TimeFormat.h
    ${SRC_DIR}/util/Version.h
	${SRC_DIR}/update/UpdateChecker.h
    ${SRC_DIR}/telemetry/TelemetryConsentDialog.h
	${SRC_DIR}/update/SelfUpdater.h
    ${SRC_DIR}/scene/SceneDiagnostic.h
	${SRC_DIR}/scene/SceneModel.h
	${SRC_DIR}/scene/SectionInventory.h
	${SRC_DIR}/scene/StopInsertion.h
	${SRC_DIR}/scene/SceneBundle.h
	${SRC_DIR}/scene/SceneCompatibility.h
	${SRC_DIR}/scene/SceneMigration.h
	${SRC_DIR}/scene/SceneValidator.h
	${SRC_DIR}/scene/SceneImporter.h
	${SRC_DIR}/scene/SceneWriter.h
	${SRC_DIR}/scene/TrackPreview.h
    ${SRC_DIR}/util/timeutil.hpp
    ${SRC_DIR}/util/Util.hpp
    ${SRC_DIR}/util/SpeedFormat.h
    ${SRC_DIR}/util/CsvWriter.h
    ${SRC_DIR}/util/TrajectoryUtil.h
    ${SRC_DIR}/scene/SceneExporter.h
)

set(EGTRAIN_UI
    ${SRC_DIR}/app/MainWindow.ui
)

set(EGTRAIN_RESOURCES
    ${SRC_DIR}/app/resources.qrc
)

set(EGTRAIN_PLATFORM_RESOURCES)
if(APPLE)
    set(EGTRAIN_ICNS ${SRC_DIR}/resources/app/egtrain.icns)
    set_source_files_properties(${EGTRAIN_ICNS}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND EGTRAIN_PLATFORM_RESOURCES ${EGTRAIN_ICNS})
elseif(WIN32)
    set(EGTRAIN_WINDOWS_ICON ${SRC_DIR}/resources/app/egtrain.ico)
    configure_file(
        ${SRC_DIR}/resources/app/egtrain.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/egtrain.rc
        @ONLY)
    list(APPEND EGTRAIN_PLATFORM_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/egtrain.rc)
endif()

# --- Executable ---
add_executable(QEGTRAIN
    ${EGTRAIN_SOURCES}
    ${EGTRAIN_HEADERS}
    ${EGTRAIN_UI}
    ${EGTRAIN_RESOURCES}
    ${EGTRAIN_PLATFORM_RESOURCES}
)

target_compile_definitions(QEGTRAIN PRIVATE EGTRAIN_BUILD_TYPE="$<CONFIG>")

target_include_directories(QEGTRAIN PRIVATE
    ${SRC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/EGTRAIN/QEGTRAIN  # for AUTOUIC generated headers
    ${CMAKE_CURRENT_BINARY_DIR}                    # fallback for generated files
)

# The isolated telemetry smoke build compiles the sender test hook into the application, so it
# links the telemetry library that has that hook.
if(EGTRAIN_ISOLATED_TELEMETRY_SMOKE)
    set(EGTRAIN_APP_TELEMETRY egtrain_telemetry_smoke)
else()
    set(EGTRAIN_APP_TELEMETRY egtrain_telemetry)
endif()

target_link_libraries(QEGTRAIN PRIVATE
    egtrain_dispatch
    egtrain_diagrams
    egtrain_widgets
    egtrain_graphics
    ${EGTRAIN_APP_TELEMETRY}
    egtrain_update
    egtrain_railml
    egtrain_sim
    egtrain_scene
    egtrain_util
    nlohmann_json::nlohmann_json
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Charts
    Qt5::Svg
    Qt5::Network
)

message(STATUS "ZeroMQ + nlohmann_json found - traffic-state sharing enabled")

add_executable(egtrain_update_helper
	${SRC_DIR}/update/UpdateHelper.cpp)
if(MSVC)
    set_property(TARGET egtrain_update_helper PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Platform-specific settings
if(APPLE)
    set_target_properties(QEGTRAIN PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.egtrain.simulator"
        MACOSX_BUNDLE_BUNDLE_NAME "EGTRAIN"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "egtrain.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
    # Copy canonical scene data into the bundle's Resources in a single POST_BUILD step.
    add_custom_command(TARGET QEGTRAIN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/Scenes"
            "$<TARGET_BUNDLE_CONTENT_DIR:QEGTRAIN>/Resources/Scenes"
        COMMENT "Copying Scenes data into .app bundle"
    )
else()
    add_custom_command(TARGET QEGTRAIN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/Scenes"
            "$<TARGET_FILE_DIR:QEGTRAIN>/Scenes"
        COMMENT "Copying Scenes data next to QEGTRAIN"
    )
endif()

# Apply LTO/IPO to Release builds only (keeps Debug builds fast to link/iterate)
if(EGTRAIN_IPO_SUPPORTED)
    set_target_properties(QEGTRAIN egtrain_util egtrain_scene egtrain_sim egtrain_railml egtrain_pugixml
        egtrain_graphics egtrain_widgets egtrain_update egtrain_dispatch egtrain_diagrams
        ${EGTRAIN_TELEMETRY_LIBS}
        PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "LTO: enabled for Release builds")
else()
    message(STATUS "LTO: not supported by this toolchain (${EGTRAIN_IPO_ERROR})")
endif()

if(WIN32)
    set_target_properties(QEGTRAIN PROPERTIES WIN32_EXECUTABLE TRUE)
    if(TARGET Qt5::WinMain)
        target_link_libraries(QEGTRAIN PRIVATE Qt5::WinMain)
    endif()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:10000000000")
endif()

add_executable(scene_tool ${SRC_DIR}/scene/SceneTool.cpp)
target_link_libraries(scene_tool PRIVATE egtrain_scene)
set_target_properties(scene_tool PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# --- Unit tests (opt-in) ---
option(EGTRAIN_BUILD_TESTS "Build unit tests" OFF)
if(EGTRAIN_ISOLATED_TELEMETRY_SMOKE)
    target_sources(QEGTRAIN PRIVATE ${SRC_DIR}/app/TelemetrySmokeSupport.cpp)
    target_compile_definitions(QEGTRAIN PRIVATE EGTRAIN_ISOLATED_TELEMETRY_SMOKE=1)
endif()
if(EGTRAIN_BUILD_TESTS)
    target_compile_definitions(QEGTRAIN PRIVATE EGTRAIN_BUILD_TESTS=1)
else()
    target_compile_definitions(QEGTRAIN PRIVATE EGTRAIN_BUILD_TESTS=0)
endif()
if(EGTRAIN_BUILD_TESTS OR EGTRAIN_PACKAGED_BUILD)
    enable_testing()
    add_executable(test_updatepreparation ${SRC_DIR}/tests/test_updatepreparation.cpp)
    target_link_libraries(test_updatepreparation PRIVATE egtrain_update Qt5::Core)
    add_test(NAME test_updatepreparation COMMAND test_updatepreparation)

    # Stand-ins for an installed application, which the update helper test installs and relaunches.
    add_executable(egtrain_update_probe ${SRC_DIR}/tests/update_probe.cpp)
    set(UPDATEHELPER_TEST_ARGS $<TARGET_FILE:egtrain_update_helper> $<TARGET_FILE:egtrain_update_probe>)
    if(WIN32)
        add_library(egtrain_update_probe_dll SHARED ${SRC_DIR}/tests/update_probe_dll.cpp)
        add_executable(egtrain_update_dllprobe ${SRC_DIR}/tests/update_dllprobe.cpp)
        target_link_libraries(egtrain_update_dllprobe PRIVATE egtrain_update_probe_dll)
        list(APPEND UPDATEHELPER_TEST_ARGS
            $<TARGET_FILE:egtrain_update_dllprobe> $<TARGET_FILE:egtrain_update_probe_dll>)
    endif()
    if(MSVC)
        # Static runtime like the helper, so a copied probe needs no runtime DLLs.
        foreach(probe egtrain_update_probe egtrain_update_probe_dll egtrain_update_dllprobe)
            if(TARGET ${probe})
                set_property(TARGET ${probe} PROPERTY
                    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            endif()
        endforeach()
    endif()
    add_executable(test_updatehelper ${SRC_DIR}/tests/test_updatehelper.cpp)
    target_link_libraries(test_updatehelper PRIVATE Qt5::Core)
    add_test(NAME test_updatehelper COMMAND test_updatehelper ${UPDATEHELPER_TEST_ARGS})
    set_tests_properties(test_updatehelper PROPERTIES TIMEOUT 120)
endif()
if(EGTRAIN_BUILD_TESTS)
    enable_testing()
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    message(STATUS "Python3: ${Python3_EXECUTABLE}")
    add_executable(test_timeformat ${SRC_DIR}/tests/test_timeformat.cpp)
    target_link_libraries(test_timeformat PRIVATE egtrain_util)
    add_test(NAME test_timeformat COMMAND test_timeformat)

    add_executable(test_version ${SRC_DIR}/tests/test_version.cpp)
    target_link_libraries(test_version PRIVATE egtrain_util)
    add_test(NAME test_version COMMAND test_version)

    # The telemetry tests link egtrain_telemetry_hooks, which defines the test hooks for them.
    # The dialog is compiled here, with the hooks, and in the application, without.
    add_executable(test_telemetryconsent
        ${SRC_DIR}/tests/test_telemetryconsent.cpp
        ${SRC_DIR}/telemetry/TelemetryConsentDialog.cpp)
    target_link_libraries(test_telemetryconsent PRIVATE egtrain_telemetry_hooks egtrain_update Qt5::Widgets)
    add_test(NAME test_telemetryconsent COMMAND test_telemetryconsent)
    set_tests_properties(test_telemetryconsent PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_telemetryqueue ${SRC_DIR}/tests/test_telemetryqueue.cpp)
    target_link_libraries(test_telemetryqueue PRIVATE egtrain_telemetry_hooks Qt5::Core)
    add_test(NAME test_telemetryqueue COMMAND test_telemetryqueue)

    add_executable(test_telemetrysender ${SRC_DIR}/tests/test_telemetrysender.cpp)
    target_link_libraries(test_telemetrysender PRIVATE egtrain_telemetry_hooks Qt5::Core Qt5::Gui Qt5::Network)
    add_test(NAME test_telemetrysender COMMAND test_telemetrysender)

    add_executable(test_telemetryoperations ${SRC_DIR}/tests/test_telemetryoperations.cpp)
    target_link_libraries(test_telemetryoperations PRIVATE egtrain_telemetry_hooks Qt5::Widgets Qt5::Network)
    add_test(NAME test_telemetryoperations COMMAND test_telemetryoperations)
    set_tests_properties(test_telemetryoperations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_telemetrynetwork ${SRC_DIR}/tests/test_telemetrynetwork.cpp)
    target_link_libraries(test_telemetrynetwork PRIVATE egtrain_telemetry_hooks Qt5::Core Qt5::Gui Qt5::Network)
    # Qt 5.15 loads OpenSSL 1.1 at run time. Windows does not provide it, so Qt has no TLS there.
    if(NOT WIN32)
        foreach(mode reject trusted)
            add_test(NAME test_telemetrynetwork_${mode} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/telemetry_tls_receiver.py
                $<TARGET_FILE:test_telemetrynetwork>
                ${SRC_DIR}/tests/fixtures/telemetry-loopback-cert.pem
                ${SRC_DIR}/tests/fixtures/telemetry-loopback-key.pem
                ${SRC_DIR}/tests/fixtures/telemetry-loopback-ca.pem
                ${mode})
            set_tests_properties(test_telemetrynetwork_${mode} PROPERTIES TIMEOUT 75)
        endforeach()
        set_tests_properties(test_telemetrynetwork_trusted PROPERTIES SKIP_RETURN_CODE 77)
    else()
        message(STATUS "Windows: skipping test_telemetrynetwork_reject and test_telemetrynetwork_trusted (no TLS library for Qt 5.15)")
    endif()

    add_executable(test_updatechecker ${SRC_DIR}/tests/test_updatechecker.cpp)
    target_link_libraries(test_updatechecker PRIVATE egtrain_update egtrain_util Qt5::Core Qt5::Network)
    add_test(NAME test_updatechecker COMMAND test_updatechecker)

    add_executable(test_updatestaging
        ${SRC_DIR}/tests/test_updatestaging.cpp)
    target_link_libraries(test_updatestaging PRIVATE Qt5::Core)
    add_test(NAME test_updatestaging COMMAND test_updatestaging)

    add_executable(test_guisimulationsnapshot
        ${SRC_DIR}/tests/test_guisimulationsnapshot.cpp)
    target_include_directories(test_guisimulationsnapshot PRIVATE ${SRC_DIR})
    add_test(NAME test_guisimulationsnapshot COMMAND test_guisimulationsnapshot)

	add_executable(test_trackpreview ${SRC_DIR}/tests/test_trackpreview.cpp)
	target_link_libraries(test_trackpreview PRIVATE egtrain_scene)
	add_test(NAME test_trackpreview COMMAND test_trackpreview)

    add_executable(test_speedformat ${SRC_DIR}/tests/test_speedformat.cpp)
    target_link_libraries(test_speedformat PRIVATE egtrain_util)
    add_test(NAME test_speedformat COMMAND test_speedformat)

    add_executable(test_trajectory ${SRC_DIR}/tests/test_trajectory.cpp)
    target_link_libraries(test_trajectory PRIVATE egtrain_util)
    add_test(NAME test_trajectory COMMAND test_trajectory)

    add_executable(test_runresults ${SRC_DIR}/tests/test_runresults.cpp)
    target_link_libraries(test_runresults PRIVATE egtrain_diagrams egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
    add_test(NAME test_runresults COMMAND test_runresults)

    add_executable(test_simulationworker ${SRC_DIR}/tests/test_simulationworker.cpp)
    target_link_libraries(test_simulationworker PRIVATE egtrain_dispatch egtrain_railml egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
    add_test(NAME test_simulationworker COMMAND test_simulationworker ${SRC_DIR}/Scenes/Assignment_Gvc_Gdg_Ut)

    add_executable(test_blockingtimediagram ${SRC_DIR}/tests/test_blockingtimediagram.cpp)
    target_link_libraries(test_blockingtimediagram PRIVATE egtrain_diagrams)
    add_test(NAME test_blockingtimediagram COMMAND test_blockingtimediagram)

    add_executable(test_capacityanalysis ${SRC_DIR}/tests/test_capacityanalysis.cpp)
    target_link_libraries(test_capacityanalysis PRIVATE egtrain_diagrams)
    add_test(NAME test_capacityanalysis COMMAND test_capacityanalysis)

    add_executable(test_routediagramcoordinates ${SRC_DIR}/tests/test_routediagramcoordinates.cpp)
    target_link_libraries(test_routediagramcoordinates PRIVATE egtrain_diagrams egtrain_util)
    add_test(NAME test_routediagramcoordinates COMMAND test_routediagramcoordinates)

    add_executable(test_diagramwindow ${SRC_DIR}/tests/test_diagramwindow.cpp)
    target_include_directories(test_diagramwindow PRIVATE ${SRC_DIR})
    target_link_libraries(test_diagramwindow PRIVATE egtrain_diagrams egtrain_telemetry egtrain_util Qt5::Network Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
    target_compile_definitions(test_diagramwindow PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_diagramwindow COMMAND test_diagramwindow)
    set_tests_properties(test_diagramwindow PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_timetabletablewindow ${SRC_DIR}/tests/test_timetabletablewindow.cpp)
    target_link_libraries(test_timetabletablewindow PRIVATE egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt5::Network Qt5::Core Qt5::Gui Qt5::Widgets)
    target_compile_definitions(test_timetabletablewindow PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_timetabletablewindow COMMAND test_timetabletablewindow)
    set_tests_properties(test_timetabletablewindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_visualpolish ${SRC_DIR}/tests/test_visualpolish.cpp)
    target_link_libraries(test_visualpolish PRIVATE egtrain_graphics Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_visualpolish COMMAND test_visualpolish)

    add_executable(test_networklegend ${SRC_DIR}/tests/test_networklegend.cpp)
    target_link_libraries(test_networklegend PRIVATE egtrain_widgets egtrain_graphics Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_networklegend COMMAND test_networklegend)
    set_tests_properties(test_networklegend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_stationoverlay ${SRC_DIR}/tests/test_stationoverlay.cpp)
    target_include_directories(test_stationoverlay PRIVATE ${SRC_DIR})
    target_link_libraries(test_stationoverlay PRIVATE egtrain_graphics Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_stationoverlay COMMAND test_stationoverlay)
    set_tests_properties(test_stationoverlay PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_networkscene_selection ${SRC_DIR}/tests/test_networkscene_selection.cpp)
    target_link_libraries(test_networkscene_selection PRIVATE
        egtrain_graphics Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_networkscene_selection COMMAND test_networkscene_selection)
    set_tests_properties(test_networkscene_selection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_playbackprofiler
        ${SRC_DIR}/tests/test_playbackprofiler.cpp)
    target_link_libraries(test_playbackprofiler PRIVATE egtrain_util)
    add_test(NAME test_playbackprofiler COMMAND test_playbackprofiler)
    add_test(NAME test_playback_profile
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/performance/test_playback_profile.py)

    add_executable(test_networkview ${SRC_DIR}/tests/test_networkview.cpp)
    target_include_directories(test_networkview PRIVATE ${SRC_DIR})
    target_link_libraries(test_networkview PRIVATE egtrain_graphics egtrain_util Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_networkview COMMAND test_networkview)
    set_tests_properties(test_networkview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_compactdoublespinbox
        ${SRC_DIR}/tests/test_compactdoublespinbox.cpp)
    target_link_libraries(test_compactdoublespinbox PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_compactdoublespinbox COMMAND test_compactdoublespinbox)
    set_tests_properties(test_compactdoublespinbox PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_colorchoicebutton ${SRC_DIR}/tests/test_colorchoicebutton.cpp)
    target_link_libraries(test_colorchoicebutton PRIVATE egtrain_widgets Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_colorchoicebutton COMMAND test_colorchoicebutton)
    set_tests_properties(test_colorchoicebutton PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_aboutdialog ${SRC_DIR}/tests/test_aboutdialog.cpp)
    target_link_libraries(test_aboutdialog PRIVATE egtrain_widgets Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_aboutdialog COMMAND test_aboutdialog)
    set_tests_properties(test_aboutdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_dialoglayout
        ${SRC_DIR}/tests/test_dialoglayout.cpp)
    target_link_libraries(test_dialoglayout PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    target_compile_definitions(test_dialoglayout PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_dialoglayout COMMAND test_dialoglayout)
    set_tests_properties(test_dialoglayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_choicecombo
        ${SRC_DIR}/tests/test_choicecombo.cpp)
    target_link_libraries(test_choicecombo PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    target_compile_definitions(test_choicecombo PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_choicecombo COMMAND test_choicecombo)
    set_tests_properties(test_choicecombo PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_timeprogressbar ${SRC_DIR}/tests/test_timeprogressbar.cpp)
    target_link_libraries(test_timeprogressbar PRIVATE egtrain_widgets egtrain_util Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_timeprogressbar COMMAND test_timeprogressbar)
    set_tests_properties(test_timeprogressbar PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

	    add_executable(test_highlighteffect ${SRC_DIR}/tests/test_highlighteffect.cpp)
	    target_link_libraries(test_highlighteffect PRIVATE egtrain_graphics Qt5::Core Qt5::Gui Qt5::Widgets)
	    add_test(NAME test_highlighteffect COMMAND test_highlighteffect)
	    set_tests_properties(test_highlighteffect PROPERTIES
	        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_csvwriter ${SRC_DIR}/tests/test_csvwriter.cpp)
    target_link_libraries(test_csvwriter PRIVATE egtrain_util)
    add_test(NAME test_csvwriter COMMAND test_csvwriter)

    add_executable(test_tractioncurve ${SRC_DIR}/tests/test_tractioncurve.cpp)
    target_include_directories(test_tractioncurve PRIVATE ${SRC_DIR})
    target_link_libraries(test_tractioncurve PRIVATE egtrain_diagrams)
    add_test(NAME test_tractioncurve COMMAND test_tractioncurve)

    add_executable(test_consolestream ${SRC_DIR}/tests/test_consolestream.cpp)
    target_link_libraries(test_consolestream PRIVATE egtrain_widgets Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_consolestream COMMAND test_consolestream)

    add_test(NAME test_qt_resources
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/e2e/test_qt_resources.py)
    add_test(NAME test_app_icon_config
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/e2e/test_app_icon_config.py)
    add_executable(test_qt_resource_images
        ${SRC_DIR}/tests/test_qt_resource_images.cpp
        ${EGTRAIN_RESOURCES})
    target_link_libraries(test_qt_resource_images PRIVATE Qt5::Core Qt5::Gui Qt5::Svg)
    add_test(NAME test_qt_resource_images COMMAND test_qt_resource_images)
    set_tests_properties(test_qt_resource_images PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    add_executable(test_routestations ${SRC_DIR}/tests/test_routestations.cpp)
    target_link_libraries(test_routestations PRIVATE egtrain_scene)
    add_test(NAME test_routestations COMMAND test_routestations ${SRC_DIR}/Scenes)

    add_executable(test_routereferencechoice ${SRC_DIR}/tests/test_routereferencechoice.cpp)
    target_link_libraries(test_routereferencechoice PRIVATE egtrain_diagrams egtrain_telemetry egtrain_scene Qt5::Network Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_routereferencechoice COMMAND test_routereferencechoice ${SRC_DIR}/Scenes)
    set_tests_properties(test_routereferencechoice PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_scenevalidator ${SRC_DIR}/tests/test_scenevalidator.cpp)
    target_link_libraries(test_scenevalidator PRIVATE egtrain_scene)
    add_test(NAME test_scenevalidator
        COMMAND test_scenevalidator ${SRC_DIR}/tests/fixtures/scenes/minimal)

    add_executable(test_stopinsertion ${SRC_DIR}/tests/test_stopinsertion.cpp)
    target_link_libraries(test_stopinsertion PRIVATE egtrain_scene)
    add_test(NAME test_stopinsertion COMMAND test_stopinsertion)

    add_executable(test_sceneimporter ${SRC_DIR}/tests/test_sceneimporter.cpp)
    target_link_libraries(test_sceneimporter PRIVATE egtrain_scene)
	add_test(NAME test_sceneimporter COMMAND test_sceneimporter)

	add_executable(test_scenewriter ${SRC_DIR}/tests/test_scenewriter.cpp)
	target_link_libraries(test_scenewriter PRIVATE egtrain_scene)
	add_test(NAME test_scenewriter
		COMMAND test_scenewriter ${SRC_DIR}/Scenes)

	add_executable(test_sceneexporter ${SRC_DIR}/tests/test_sceneexporter.cpp)
	target_link_libraries(test_sceneexporter PRIVATE egtrain_scene)
	add_test(NAME test_sceneexporter COMMAND test_sceneexporter)

	add_executable(test_scenebundle ${SRC_DIR}/tests/test_scenebundle.cpp)
	target_link_libraries(test_scenebundle PRIVATE egtrain_scene)
	add_test(NAME test_scenebundle
		COMMAND test_scenebundle ${SRC_DIR}/tests/fixtures/scenes/minimal)

	add_executable(test_scenecompatibility ${SRC_DIR}/tests/test_scenecompatibility.cpp)
	target_link_libraries(test_scenecompatibility PRIVATE egtrain_scene)
	add_test(NAME test_scenecompatibility
		COMMAND test_scenecompatibility ${SRC_DIR}/tests/fixtures/scenes/minimal)

	add_executable(test_scenebuilder ${SRC_DIR}/tests/test_scenebuilder.cpp)
	target_link_libraries(test_scenebuilder PRIVATE egtrain_sim)
	add_test(NAME test_scenebuilder COMMAND test_scenebuilder)

	add_executable(test_operationsbuilder ${SRC_DIR}/tests/test_operationsbuilder.cpp)
	target_link_libraries(test_operationsbuilder PRIVATE egtrain_diagrams egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
	add_test(NAME test_operationsbuilder COMMAND test_operationsbuilder)

	# Behaviour characterization of the simulation core (see tests/characterization).
	add_subdirectory(EGTRAIN/QEGTRAIN/tests/characterization)

	add_executable(test_railmlparser ${SRC_DIR}/tests/test_railmlparser.cpp)
	# The test defines initial_variables, so egtrain_sim's Infrastructure.cpp member must stay out of the link.
	target_link_libraries(test_railmlparser PRIVATE egtrain_railml egtrain_sim cppzmq nlohmann_json::nlohmann_json)
	add_test(NAME test_railmlparser COMMAND test_railmlparser)
	add_test(NAME test_route_choice_missing_data
		COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Assignment_Gvc_Gdg_Ut
			-g 0 -h 1 -TSM 0 -RC 1)
	set_tests_properties(test_route_choice_missing_data PROPERTIES
		PASS_REGULAR_EXPRESSION "scene.route_choice.passengers.none"
		TIMEOUT 30)
	add_test(NAME test_external_sharing_no_listener
		COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Paimpol
			-g 0 -h 1 -TSM 1 -RC 1)
	set_tests_properties(test_external_sharing_no_listener PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/external-sharing-test"
		TIMEOUT 30)

	add_test(NAME test_headless_smoke_decode
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_headless_smoke_decode.py)
	add_test(NAME test_scene_tool_diagnostics
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_scene_tool_diagnostics.py $<TARGET_FILE:scene_tool>)
	add_test(NAME test_startup_launch_contract
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/startup_launch_contract.py $<TARGET_FILE:QEGTRAIN>)
	# Cover two 60-second child launches, two 8-second PTY checks, and cleanup.
	set_tests_properties(test_startup_launch_contract PROPERTIES TIMEOUT 150)
	if(NOT WIN32)
		add_test(NAME test_case_chooser_contract
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/case_chooser_contract.py
				$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/Scenes/Paimpol)
		set_tests_properties(test_case_chooser_contract PROPERTIES TIMEOUT 120)
	else()
		message(STATUS "Windows: skipping test_case_chooser_contract (path forms, INI escapes, directory symlink)")
	endif()
	# This test needs the unsaved-changes prompt. Qt 5.15 crashes when a QMessageBox is shown on
	# the offscreen platform on Windows, so the test uses the native platform there.
	if(WIN32)
		set(egtrain_scene_drop_platform windows)
	else()
		set(egtrain_scene_drop_platform offscreen)
	endif()
	add_test(NAME test_scene_drop
		COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Paimpol)
	set_tests_properties(test_scene_drop PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=${egtrain_scene_drop_platform};QEGTRAIN_E2E_SCENE_DROP=1;QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scene-drop-test"
		PASS_REGULAR_EXPRESSION "E2E_SCENE_DROP_OK"
		TIMEOUT 60)
	add_test(NAME test_native_startup_without_legacy
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_case3_startup_errors.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_native_startup_without_legacy PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 120)
	add_test(NAME test_runtime_memory_safety
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/runtime_memory_safety_smoke.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_runtime_memory_safety PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 240)
	add_test(NAME test_simulation_determinism
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/simulation_determinism_smoke.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_simulation_determinism PROPERTIES TIMEOUT 300)
	add_test(NAME test_station_departure
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/station_departure_smoke.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_station_departure PROPERTIES TIMEOUT 300)
	add_test(NAME test_gui_autostart_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/gui_autostart_smoke.py $<TARGET_FILE:QEGTRAIN>)
	# Release completes in seconds; the ceiling exists for the instrumented
	# sanitizer job, which needs room for the full run plus a short liveness hold.
	set_tests_properties(test_gui_autostart_smoke PROPERTIES TIMEOUT 420)
	add_test(NAME test_close_during_run_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/close_during_run_smoke.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_close_during_run_smoke PROPERTIES TIMEOUT 240)
	if(UNIX)
		add_test(NAME test_csv_export_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/csv_export_smoke.sh)
		set_tests_properties(test_csv_export_smoke PROPERTIES TIMEOUT 240)
		add_test(NAME test_lebanon_scene_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/lebanon_scene_smoke.sh)
		set_tests_properties(test_lebanon_scene_smoke PROPERTIES TIMEOUT 360)
		add_test(NAME test_creator_acceptance_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/creator_acceptance_smoke.sh)
		set_tests_properties(test_creator_acceptance_smoke PROPERTIES TIMEOUT 240)
		set_tests_properties(test_csv_export_smoke test_lebanon_scene_smoke test_creator_acceptance_smoke
			PROPERTIES ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>;QEGTRAIN_SCENE_TOOL=$<TARGET_FILE:scene_tool>")
	else()
		message(STATUS "Windows: skipping bash smoke tests (test_csv_export_smoke, test_lebanon_scene_smoke, test_creator_acceptance_smoke)")
	endif()
	if(APPLE)
		add_test(NAME test_package_contents_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/package_contents_smoke.sh)
		set_tests_properties(test_package_contents_smoke PROPERTIES TIMEOUT 420)
	endif()
	add_test(NAME test_release_version
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_version.py)
	add_test(NAME test_ci_workflow
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_ci_workflow.py)
	add_test(NAME test_signalling_bounds
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_signalling_bounds.py)
	add_test(NAME test_windows_subsystem_config
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_windows_subsystem_config.py)
	add_test(NAME test_raii_contract
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_raii_contract.py)
	add_test(NAME test_ownership_inventory
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_ownership_inventory.py)
	if(NOT WIN32)
		add_test(NAME test_measure_peak_rss
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_measure_peak_rss.py)
	else()
		message(STATUS "Windows: skipping test_measure_peak_rss (macOS /usr/bin/time collector)")
	endif()

	# Headless run of a built scene. The scene path is relative to the executable.
	if(APPLE)
		set(EGTRAIN_SMOKE_APP_SCENE "../Resources/Scenes/Paimpol")
	else()
		set(EGTRAIN_SMOKE_APP_SCENE "Scenes/Paimpol")
	endif()
	add_test(NAME test_headless_scene_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
			$<TARGET_FILE:QEGTRAIN> ${EGTRAIN_SMOKE_APP_SCENE})
	set_tests_properties(test_headless_scene_smoke PROPERTIES TIMEOUT 150)

	# A Windows EXE image near 2 GiB fails to start, so the limit is checked on every Windows build.
	if(WIN32)
		set(EGTRAIN_MAX_PE_IMAGE_BYTES 1600000000 CACHE STRING
			"Fail test_windows_image_size when QEGTRAIN.exe SizeOfImage exceeds this many bytes")
		add_test(NAME test_windows_image_size
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/pe_image_size.py
				$<TARGET_FILE:QEGTRAIN> --max-bytes ${EGTRAIN_MAX_PE_IMAGE_BYTES}
				--machine x64 --subsystem windows)
	endif()
	add_test(NAME test_pe_image_size
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_pe_image_size.py)
	add_test(NAME test_package_start_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_package_start_smoke.py)
	# A 32-bit configure stops with the message that names the supported platform.
	if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
		add_test(NAME test_win32_configure_rejected
			COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}
				-B ${CMAKE_CURRENT_BINARY_DIR}/win32-configure-check -G ${CMAKE_GENERATOR} -A Win32)
		set_tests_properties(test_win32_configure_rejected PROPERTIES
			PASS_REGULAR_EXPRESSION "EGTRAIN supports 64-bit Windows"
			TIMEOUT 300)
	endif()

	# Canonical V1 is the product path. Legacy tests cover only converter and
	# compatibility boundaries and can be selected with `ctest -L legacy-compat`.
	set_tests_properties(test_scenevalidator test_routestations test_stopinsertion test_scenewriter test_scenebuilder test_operationsbuilder test_scene_tool_diagnostics test_trackpreview
		PROPERTIES LABELS "scene-v1")
	set_tests_properties(test_sceneimporter test_sceneexporter test_headless_smoke_decode
		PROPERTIES LABELS "legacy-compat")
	set_tests_properties(test_scenebundle test_scenecompatibility PROPERTIES LABELS "scene-v2")

	# Every test is either `unit` (in-process or a static check) or `integration`
	# (starts or inspects a built program, or talks to a loopback server).
	# `gui` needs a Qt platform plugin and `slow` takes more than 20 s locally.
	# APPEND keeps the scene labels above. Names not registered on this platform are skipped.
	function(egtrain_label_tests label)
		foreach(name IN LISTS ARGN)
			if(TEST ${name})
				set_property(TEST ${name} APPEND PROPERTY LABELS ${label})
			endif()
		endforeach()
	endfunction()
	egtrain_label_tests(unit
		test_updatepreparation test_timeformat test_version test_telemetryconsent test_telemetryqueue
		test_telemetrysender test_telemetryoperations test_updatechecker test_updatestaging
		test_guisimulationsnapshot test_simulationworker test_trackpreview test_speedformat test_trajectory test_runresults
		test_blockingtimediagram test_capacityanalysis test_routediagramcoordinates test_diagramwindow
		test_routestations test_routereferencechoice
		test_timetabletablewindow test_visualpolish test_networklegend test_stationoverlay
		test_networkscene_selection test_playbackprofiler test_playback_profile test_networkview
		test_compactdoublespinbox test_colorchoicebutton test_aboutdialog test_dialoglayout test_choicecombo test_timeprogressbar
		test_highlighteffect test_csvwriter test_tractioncurve test_consolestream test_qt_resources
		test_app_icon_config test_qt_resource_images test_scenevalidator test_stopinsertion test_sceneimporter
		test_scenewriter test_sceneexporter test_scenebundle test_scenecompatibility test_scenebuilder
		test_operationsbuilder test_railmlparser test_headless_smoke_decode test_release_version
		test_ci_workflow test_signalling_bounds test_windows_subsystem_config test_raii_contract
		test_ownership_inventory test_measure_peak_rss test_pe_image_size test_package_start_smoke)
	egtrain_label_tests(integration
		test_telemetrynetwork_reject test_telemetrynetwork_trusted test_updatehelper
		test_route_choice_missing_data test_external_sharing_no_listener test_scene_tool_diagnostics
		test_startup_launch_contract test_case_chooser_contract test_scene_drop
		test_native_startup_without_legacy test_runtime_memory_safety test_simulation_determinism test_station_departure
		test_gui_autostart_smoke test_close_during_run_smoke test_csv_export_smoke test_lebanon_scene_smoke
		test_creator_acceptance_smoke test_package_contents_smoke test_headless_scene_smoke
		test_windows_image_size test_win32_configure_rejected)
	egtrain_label_tests(gui
		test_telemetryconsent test_telemetryoperations test_diagramwindow test_timetabletablewindow
		test_visualpolish test_networklegend test_stationoverlay test_networkscene_selection
		test_networkview test_compactdoublespinbox test_colorchoicebutton test_aboutdialog test_dialoglayout test_choicecombo
		test_routereferencechoice test_timeprogressbar test_highlighteffect test_qt_resource_images test_scene_drop
		test_startup_launch_contract test_case_chooser_contract test_gui_autostart_smoke
		test_close_during_run_smoke test_csv_export_smoke test_creator_acceptance_smoke
		test_package_contents_smoke)
	egtrain_label_tests(slow test_gui_autostart_smoke test_ownership_inventory)

	# Fail configuration unless a test has exactly one of the unit and integration labels.
	get_property(egtrain_all_tests DIRECTORY PROPERTY TESTS)
	foreach(name IN LISTS egtrain_all_tests)
		get_test_property(${name} LABELS egtrain_test_labels)
		set(egtrain_kind_count 0)
		foreach(kind unit integration)
			if(egtrain_test_labels MATCHES "(^|;)${kind}(;|$)")
				math(EXPR egtrain_kind_count "${egtrain_kind_count} + 1")
			endif()
		endforeach()
		if(NOT egtrain_kind_count EQUAL 1)
			message(FATAL_ERROR "Test ${name} needs exactly one of the labels unit and integration")
		endif()
	endforeach()

	# The offscreen Qt platform has no fonts of its own on Windows, so GUI tests read the system fonts.
	if(WIN32)
		file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" egtrain_windows_fonts)
		foreach(name IN LISTS egtrain_all_tests)
			get_test_property(${name} LABELS egtrain_test_labels)
			if(egtrain_test_labels MATCHES "(^|;)gui(;|$)")
				set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=${egtrain_windows_fonts}")
			endif()
		endforeach()
	endif()
endif()

# Install
install(TARGETS QEGTRAIN scene_tool
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
    FRAMEWORK DESTINATION .)
install(DIRECTORY ${SRC_DIR}/Scenes/ DESTINATION share/EGTRAIN/Scenes)
