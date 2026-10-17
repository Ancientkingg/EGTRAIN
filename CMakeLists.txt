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
option(EGTRAIN_BUILD_TESTS "Build unit tests" OFF)
option(EGTRAIN_IPO "Link Release builds with interprocedural optimization (LTO, or LTCG with MSVC) when the toolchain supports it" ON)
option(EGTRAIN_NETWORK_TESTS "Register the tests that need internet access" OFF)
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
# EGTRAIN_IPO=OFF skips it: the link step then needs no code generation, which a compiler cache cannot store.
if(EGTRAIN_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT EGTRAIN_IPO_SUPPORTED OUTPUT EGTRAIN_IPO_ERROR)
else()
    set(EGTRAIN_IPO_SUPPORTED FALSE)
    set(EGTRAIN_IPO_ERROR "EGTRAIN_IPO is OFF")
endif()

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
find_package(Threads REQUIRED)

# --- Warnings ---
# egtrain_target_warnings(<target> [STRICT]) adds -Wall -Wextra (/W4 on MSVC) to the C++ sources
# of one target. The flags are PRIVATE, so a target that links the library does not get them.
# STRICT also turns a warning into an error when EGTRAIN_WARNINGS_AS_ERRORS is ON.
option(EGTRAIN_WARNINGS_AS_ERRORS "Treat warnings in the strict targets as errors" OFF)
function(egtrain_target_warnings target)
    cmake_parse_arguments(ARG "STRICT" "" "" ${ARGN})
    if(MSVC)
        set(egtrain_warning_flags /W4)
        set(egtrain_error_flags /WX)
        # MSVC reports the standard functions getenv, sscanf and strcpy as deprecated (C4996).
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(egtrain_warning_flags -Wall -Wextra)
        set(egtrain_error_flags -Werror)
    else()
        return()
    endif()
    if(ARG_STRICT AND EGTRAIN_WARNINGS_AS_ERRORS)
        list(APPEND egtrain_warning_flags ${egtrain_error_flags})
    endif()
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${egtrain_warning_flags}>")
endfunction()

# --- Model libraries ---
# Each source below is compiled once. The libraries are static, so a target that links one gets
# only the objects it references. A target that pulls in Infrastructure,
# Signalling or RollingStock from egtrain_sim must define `Logger owl`.
add_library(egtrain_pugixml STATIC ${SRC_DIR}/io/third_party/pugixml.cpp)

add_library(egtrain_util STATIC
    ${SRC_DIR}/util/CsvWriter.cpp
    ${SRC_DIR}/util/Log.cpp
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
    ${SRC_DIR}/scene/SceneFieldReader.cpp
    ${SRC_DIR}/scene/SceneHistory.cpp
    ${SRC_DIR}/scene/SceneIds.cpp
    ${SRC_DIR}/scene/SceneImporter.cpp
    ${SRC_DIR}/scene/SceneMigration.cpp
    ${SRC_DIR}/scene/SceneModel.cpp
    ${SRC_DIR}/scene/SceneReferences.cpp
    ${SRC_DIR}/scene/SceneValidator.cpp
    ${SRC_DIR}/scene/SceneWriter.cpp
    ${SRC_DIR}/scene/SectionInventory.cpp
    ${SRC_DIR}/scene/StopInsertion.cpp
    ${SRC_DIR}/scene/TrackPreview.cpp)
target_link_libraries(egtrain_scene
    PUBLIC nlohmann_json::nlohmann_json egtrain_miniz)

# RollingStockTrajectory.cpp follows RollingStock.cpp instead of its alphabetical place. In that place the
# unchanged braking functions were linked at other alignments and Copenhagen ran about 3.6 percent slower.
# OperationsBuilder.cpp likewise stays right behind RollingStock.cpp, where its code used to be: at the end of the
# list the Netherlands run was about 4 percent slower.
add_library(egtrain_sim STATIC
    ${SRC_DIR}/simulation/Infrastructure.cpp
    ${SRC_DIR}/simulation/InitialParameters.cpp
    ${SRC_DIR}/simulation/NumberGenerator.cpp
    ${SRC_DIR}/simulation/Optimisation.cpp
    ${SRC_DIR}/simulation/Passengers.cpp
    ${SRC_DIR}/simulation/RollingStock.cpp
    ${SRC_DIR}/simulation/builders/OperationsBuilder.cpp
    ${SRC_DIR}/simulation/RollingStockTrajectory.cpp
    ${SRC_DIR}/simulation/RollingStockBlockingTimes.cpp
    ${SRC_DIR}/simulation/RollingStockBraking.cpp
    ${SRC_DIR}/simulation/RollingStockDynamics.cpp
    ${SRC_DIR}/simulation/RollingStockOccupation.cpp
    ${SRC_DIR}/simulation/RollingStockStops.cpp
    ${SRC_DIR}/simulation/Signalling.cpp
    ${SRC_DIR}/simulation/Simulation.cpp
    ${SRC_DIR}/simulation/SimulationRun.cpp
    ${SRC_DIR}/simulation/builders/InfrastructureBuilder.cpp)
target_link_libraries(egtrain_sim
    PUBLIC egtrain_scene egtrain_util nlohmann_json::nlohmann_json)

# RttpXml.cpp uses `initial_variables`, which egtrain_sim or the target itself defines.
# List egtrain_rttp before egtrain_sim.
add_library(egtrain_rttp STATIC ${SRC_DIR}/io/RttpXml.cpp ${SRC_DIR}/io/ExternalState.cpp)
target_link_libraries(egtrain_rttp
    PUBLIC egtrain_pugixml nlohmann_json::nlohmann_json
    PRIVATE cppzmq)

# No Qt: the frames of the live contract, built from a GuiSimulationSnapshot, the checks on the requests of its clients,
# and the server that publishes the frames and takes the requests.
add_library(egtrain_live STATIC ${SRC_DIR}/io/LiveCommand.cpp ${SRC_DIR}/io/LiveFrame.cpp ${SRC_DIR}/io/LiveServer.cpp)
target_link_libraries(egtrain_live
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE egtrain_util cppzmq Threads::Threads)

foreach(egtrain_lib egtrain_pugixml egtrain_util egtrain_scene egtrain_sim egtrain_rttp egtrain_live)
    target_include_directories(${egtrain_lib} PUBLIC ${SRC_DIR})
    set_target_properties(${egtrain_lib} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
endforeach()

# --- Qt libraries ---
# Each source is compiled once, as for the model libraries. These libraries have Q_OBJECT
# classes, so AUTOMOC stays on. A target that uses one class of a library also gets the other
# classes of that library, because their moc code is in one object. None depends on MainWindow.
add_library(egtrain_graphics STATIC
    ${SRC_DIR}/graphics/FollowCamera.cpp
    ${SRC_DIR}/graphics/NetworkPresenter.cpp
    ${SRC_DIR}/graphics/NetworkScene.cpp
    ${SRC_DIR}/graphics/NetworkView.cpp
    ${SRC_DIR}/graphics/VisualPolish.cpp
    ${SRC_DIR}/graphics/items/ConnectionItem.cpp
    ${SRC_DIR}/graphics/items/HighlightEffect.cpp
    ${SRC_DIR}/graphics/items/IconItem.cpp
    ${SRC_DIR}/graphics/items/NodeItem.cpp
    ${SRC_DIR}/graphics/items/PassengerItem.cpp
    ${SRC_DIR}/graphics/items/PlatformItem.cpp
    ${SRC_DIR}/graphics/items/SelectionCueItem.cpp
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
    ${SRC_DIR}/widgets/CaseChooserDialog.cpp
    ${SRC_DIR}/widgets/ColorChoiceButton.cpp
    ${SRC_DIR}/widgets/ConsoleWidget.cpp
    ${SRC_DIR}/widgets/InfoDockWidget.cpp
    ${SRC_DIR}/widgets/NetworkLegendWidget.cpp
    ${SRC_DIR}/widgets/TimeProgressBar.cpp)
target_link_libraries(egtrain_widgets
    PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_graphics egtrain_util)

# The scene document and the panels of the scene editors. SceneEditorPanel.h has no source file, so it is listed here for moc.
add_library(egtrain_editors STATIC
    ${SRC_DIR}/editors/CaseSettingsPanel.cpp
    ${SRC_DIR}/editors/CompositionPanel.cpp
    ${SRC_DIR}/editors/EntranceDelayPanel.cpp
    ${SRC_DIR}/editors/IncidentPanel.cpp
    ${SRC_DIR}/editors/InfrastructurePanel.cpp
    ${SRC_DIR}/editors/infrastructure/TopologyFacets.cpp
    ${SRC_DIR}/editors/PassengerPanel.cpp
    ${SRC_DIR}/editors/SceneDocument.cpp
    ${SRC_DIR}/editors/SceneEditorPanel.h
    ${SRC_DIR}/editors/ScenarioPanel.cpp
    ${SRC_DIR}/editors/ServicePanel.cpp
    ${SRC_DIR}/editors/TimetableStopsPanel.cpp
    ${SRC_DIR}/editors/TrainUnitPanel.cpp
    ${SRC_DIR}/editors/TrainUnitSourceLinker.cpp)
target_link_libraries(egtrain_editors
    PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_scene egtrain_diagrams egtrain_widgets egtrain_runselection egtrain_util)

# The HTTP requests of the update check and the telemetry sender. Qt 5.15 has no TLS library on
# Windows, so the requests go through WinHTTP there.
add_library(egtrain_http STATIC ${SRC_DIR}/update/HttpTransport.cpp)
target_link_libraries(egtrain_http PUBLIC Qt5::Core Qt5::Network)
if(WIN32)
    target_sources(egtrain_http PRIVATE ${SRC_DIR}/update/WinHttpTransport.cpp)
    target_link_libraries(egtrain_http PUBLIC winhttp crypt32)
endif()

# The update code that both the application and a test use. SelfUpdater and UpdateNotices belong
# to the application alone.
add_library(egtrain_update STATIC
    ${SRC_DIR}/update/Distribution.cpp
    ${SRC_DIR}/update/ReleaseInfo.cpp
    ${SRC_DIR}/update/UpdateChecker.cpp
    ${SRC_DIR}/update/UpdatePreparation.cpp
    ${SRC_DIR}/update/UpdateRecord.cpp
    ${SRC_DIR}/update/UpdateSettings.cpp)
target_link_libraries(egtrain_update PUBLIC Qt5::Core Qt5::Network egtrain_http egtrain_util)

# What the next run uses of the scene apart from the scene itself: the selected scenario and the service
# occurrences that are left out. No Qt; the application and test_runselection link it.
add_library(egtrain_runselection STATIC ${SRC_DIR}/app/RunSelection.cpp)
target_link_libraries(egtrain_runselection PUBLIC egtrain_scene)

# What a command line asks the application to open. Qt Core only; the application and
# test_sceneopenrequest link it.
add_library(egtrain_sceneopen STATIC ${SRC_DIR}/app/SceneOpenRequest.cpp)
target_link_libraries(egtrain_sceneopen PUBLIC Qt5::Core)

# Hands a file that the system asked the application to open to the code that opens it: from a second
# process to the running instance, or as an open event. Qt Core, Gui (QFileOpenEvent) and Network
# only; the application and test_openrequestrelay link it.
add_library(egtrain_openrelay STATIC ${SRC_DIR}/app/OpenRequestRelay.cpp)
target_link_libraries(egtrain_openrelay PUBLIC Qt5::Core Qt5::Gui Qt5::Network)

# Recovery copies of a scene with unsaved edits, kept below a folder that the caller names, the
# controller that writes them in the background, and the dialog that offers them. Only the recovery
# tests link it.
add_library(egtrain_recovery STATIC
    ${SRC_DIR}/recovery/RecoveryController.cpp
    ${SRC_DIR}/recovery/RecoveryPrompt.cpp
    ${SRC_DIR}/recovery/RecoveryStore.cpp)
target_link_libraries(egtrain_recovery PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_scene)

# The test hooks change the layout of TelemetrySender, TelemetryConsent and TelemetryQueue, so a
# build with hooks compiles the same sources again, by design:
#   egtrain_telemetry        no hooks: the application and the diagram window tests
#   egtrain_telemetry_hooks  all three hooks, for every telemetry test (tests only)
#   egtrain_telemetry_smoke  the sender hook: the application in the isolated smoke build
# A target links at most one of them.
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
    target_link_libraries(${egtrain_lib} PUBLIC Qt5::Core Qt5::Gui Qt5::Network Qt5::Widgets egtrain_http)
endforeach()

# The application, test_simulationworker and test_characterization run the simulation through these.
# They need `Logger owl` from the target, like egtrain_sim.
add_library(egtrain_dispatch STATIC
    ${SRC_DIR}/app/DispatchController.cpp
    ${SRC_DIR}/app/GuiReplayHistory.cpp
    ${SRC_DIR}/simulation/RunControl.cpp
    ${SRC_DIR}/simulation/SimulationWorker.cpp)
target_link_libraries(egtrain_dispatch
    PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets egtrain_live egtrain_rttp egtrain_sim)

# DiagramWindow and TimetableTableWindow call telemetry::OperationObservation, whose symbols do
# not depend on the hooks, so this library links no telemetry flavour. In the isolated telemetry
# smoke configuration it is built with the sender hook, as the rest of the application is. The
# moc object of the library references both windows, so a target that uses TrainFilterButton,
# DiagramWindow or TimetableTableWindow also links one telemetry library. So does a target that
# uses AreaCapacityWindow or DelayAnalysisWindow, which call it as well.
add_library(egtrain_diagrams STATIC
    ${SRC_DIR}/diagrams/AreaCapacity.cpp
    ${SRC_DIR}/diagrams/AreaCapacityWindow.cpp
    ${SRC_DIR}/diagrams/BlockingTimeDiagram.cpp
    ${SRC_DIR}/diagrams/BlockingTimeScope.cpp
    ${SRC_DIR}/diagrams/CapacityAnalysis.cpp
    ${SRC_DIR}/diagrams/CapacityScope.cpp
    ${SRC_DIR}/diagrams/CompletedRunCapture.cpp
    ${SRC_DIR}/diagrams/DelayAnalysis.cpp
    ${SRC_DIR}/diagrams/DelayAnalysisWindow.cpp
    ${SRC_DIR}/diagrams/DiagramWindow.cpp
    ${SRC_DIR}/diagrams/RouteDiagramCoordinates.cpp
    ${SRC_DIR}/diagrams/RouteReferenceChoice.cpp
    ${SRC_DIR}/diagrams/RunExports.cpp
    ${SRC_DIR}/diagrams/RunResults.cpp
    ${SRC_DIR}/diagrams/TimetableTableWindow.cpp
    ${SRC_DIR}/diagrams/TractionCurve.cpp
    ${SRC_DIR}/diagrams/TrainColors.cpp
    ${SRC_DIR}/diagrams/TrainFilterButton.cpp)
target_link_libraries(egtrain_diagrams
    PUBLIC Qt5::Core Qt5::Gui Qt5::Network Qt5::Widgets Qt5::Charts egtrain_sim)
if(EGTRAIN_ISOLATED_TELEMETRY_SMOKE)
    target_compile_definitions(egtrain_diagrams PRIVATE EGTRAIN_SENDER_TEST_HOOK=1)
endif()

foreach(egtrain_lib egtrain_graphics egtrain_widgets egtrain_editors egtrain_http egtrain_update egtrain_sceneopen egtrain_openrelay egtrain_recovery ${EGTRAIN_TELEMETRY_LIBS}
        egtrain_dispatch egtrain_diagrams)
    target_include_directories(${egtrain_lib} PUBLIC ${SRC_DIR})
    set_target_properties(${egtrain_lib} PROPERTIES
        AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)
endforeach()

# Warning flags per target. Strict targets build without a warning, so a new warning is an error
# when EGTRAIN_WARNINGS_AS_ERRORS is ON.
foreach(egtrain_lib egtrain_util egtrain_scene egtrain_runselection egtrain_live egtrain_http egtrain_update egtrain_sceneopen egtrain_openrelay egtrain_recovery egtrain_diagrams
        egtrain_graphics egtrain_widgets egtrain_editors ${EGTRAIN_TELEMETRY_LIBS})
    egtrain_target_warnings(${egtrain_lib} STRICT)
endforeach()
# Flags only: egtrain_dispatch has no warning of its own. The 57 warning lines of its build come
# from the simulation headers.
egtrain_target_warnings(egtrain_dispatch)
# No flags:
#   egtrain_pugixml, egtrain_miniz  vendored code
#   egtrain_sim, egtrain_rttp       legacy code with 104 and 4 warning lines, see #453, #454 and #482
#   QEGTRAIN                        98 warning lines, 41 of them in app/MainWindow.cpp and the rest
#                                   from the simulation headers, until the window is split up;
#                                   app/main.cpp, update/SelfUpdater.cpp,
#                                   update/UpdateNotices.cpp and telemetry/TelemetryConsentDialog.cpp
#                                   build here too; test_selfupdater builds SelfUpdater.cpp with the flags
# A test executable that uses the simulation has no flags either.

# --- Sources ---
set(EGTRAIN_SOURCES
    ${SRC_DIR}/app/main.cpp
    ${SRC_DIR}/app/MainWindow.cpp
    ${SRC_DIR}/app/RuntimeGeometryFromGlobals.cpp
    ${SRC_DIR}/app/SceneOutputDirectory.cpp
    ${SRC_DIR}/telemetry/TelemetryConsentDialog.cpp
    ${SRC_DIR}/update/SelfUpdater.cpp
    ${SRC_DIR}/update/UpdateNotices.cpp
)

set(EGTRAIN_HEADERS
    ${SRC_DIR}/widgets/DialogLayout.h
    ${SRC_DIR}/widgets/ChoiceComboBox.h
    ${SRC_DIR}/widgets/EditorLayout.h
    ${SRC_DIR}/widgets/UserPrompts.h
    ${SRC_DIR}/simulation/Infrastructure.h
    ${SRC_DIR}/simulation/InitialParameters.h
    ${SRC_DIR}/io/ExternalState.h
    ${SRC_DIR}/io/RttpXml.h
    ${SRC_DIR}/app/MainWindow.h
    ${SRC_DIR}/app/MainWindowTestHooks.h
    ${SRC_DIR}/app/RuntimeGeometryFromGlobals.h
    ${SRC_DIR}/app/SceneOutputDirectory.h
    ${SRC_DIR}/graphics/RuntimeGeometrySource.h
    ${SRC_DIR}/graphics/SignalItemRoles.h
    ${SRC_DIR}/simulation/NumberGenerator.h
    ${SRC_DIR}/simulation/Optimisation.h
    ${SRC_DIR}/util/Log.h
    ${SRC_DIR}/util/Logger.hpp
    ${SRC_DIR}/util/PlaybackProfiler.h
    ${SRC_DIR}/simulation/Passengers.h
    ${SRC_DIR}/util/portability.h
    ${SRC_DIR}/simulation/RollingStock.h
    ${SRC_DIR}/simulation/Signalling.h
    ${SRC_DIR}/simulation/Simulation.h
    ${SRC_DIR}/simulation/SimulationRun.h
    ${SRC_DIR}/util/TimeFormat.h
    ${SRC_DIR}/util/Version.h
    ${SRC_DIR}/telemetry/TelemetryConsentDialog.h
	${SRC_DIR}/update/SelfUpdater.h
	${SRC_DIR}/update/UpdateNotices.h
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
    egtrain_editors
    egtrain_diagrams
    egtrain_widgets
    egtrain_graphics
    ${EGTRAIN_APP_TELEMETRY}
    egtrain_update
    egtrain_sceneopen
    egtrain_openrelay
    egtrain_rttp
    egtrain_sim
    egtrain_runselection
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
egtrain_target_warnings(egtrain_update_helper STRICT)
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
    set_target_properties(QEGTRAIN egtrain_util egtrain_scene egtrain_sim egtrain_rttp egtrain_pugixml
        egtrain_graphics egtrain_widgets egtrain_http egtrain_update egtrain_sceneopen egtrain_openrelay egtrain_recovery egtrain_dispatch egtrain_diagrams
        ${EGTRAIN_TELEMETRY_LIBS}
        PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "LTO: enabled for Release builds")
else()
    message(STATUS "LTO: not enabled (${EGTRAIN_IPO_ERROR})")
endif()

if(WIN32)
    set_target_properties(QEGTRAIN PROPERTIES WIN32_EXECUTABLE TRUE)
    if(TARGET Qt5::WinMain)
        target_link_libraries(QEGTRAIN PRIVATE Qt5::WinMain)
    endif()
    # The main thread gets the 8 MiB it has on macOS and Linux. The reserve of an executable is
    # also the stack size of each thread that is started without one, so only the application
    # has it and the other executables keep the linker default.
    set(EGTRAIN_STACK_RESERVE_BYTES 8388608)
    if(MSVC)
        target_link_options(QEGTRAIN PRIVATE /STACK:${EGTRAIN_STACK_RESERVE_BYTES})
    endif()
endif()

add_executable(scene_tool ${SRC_DIR}/scene/SceneTool.cpp)
target_link_libraries(scene_tool PRIVATE egtrain_scene)
set_target_properties(scene_tool PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
egtrain_target_warnings(scene_tool STRICT)

# --- Unit tests (opt-in) ---
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
    target_compile_definitions(test_updatepreparation PRIVATE EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests")
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
    target_link_libraries(test_updatehelper PRIVATE egtrain_update Qt5::Core)
    add_test(NAME test_updatehelper COMMAND test_updatehelper ${UPDATEHELPER_TEST_ARGS})
    set_tests_properties(test_updatehelper PROPERTIES TIMEOUT 120)

    # One real HTTPS request to GitHub through the update transport. The package job runs the program from a
    # copy of the package; the test below runs it from the build.
    add_executable(test_updatetransport_https ${SRC_DIR}/tests/test_updatetransport_https.cpp)
    target_link_libraries(test_updatetransport_https PRIVATE egtrain_update Qt5::Core Qt5::Network)
    foreach(egtrain_test test_updatepreparation test_updatehelper test_updatetransport_https)
        egtrain_target_warnings(${egtrain_test} STRICT)
    endforeach()
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

    add_executable(test_log ${SRC_DIR}/tests/test_log.cpp)
    target_link_libraries(test_log PRIVATE egtrain_util)
    add_test(NAME test_log COMMAND test_log)

    # The telemetry tests link egtrain_telemetry_hooks, which defines the test hooks for them.
    # The dialog is compiled here, with all hooks, and in the application, without the consent hook.
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
    # The sender sends through the HTTP transport, which is WinHTTP on Windows, so the tests need no Qt TLS library there.
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
    # Qt 5 Secure Transport on macOS does not trust the root certificate of a request, and the program then returns 77.
    set_tests_properties(test_telemetrynetwork_trusted PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(test_updatechecker ${SRC_DIR}/tests/test_updatechecker.cpp)
    target_link_libraries(test_updatechecker PRIVATE egtrain_update egtrain_util Qt5::Core Qt5::Network)
    add_test(NAME test_updatechecker COMMAND test_updatechecker)

    add_executable(test_httptransport ${SRC_DIR}/tests/test_httptransport.cpp)
    target_link_libraries(test_httptransport PRIVATE egtrain_update Qt5::Core Qt5::Network)
    add_test(NAME test_httptransport COMMAND test_httptransport)
    set_tests_properties(test_httptransport PROPERTIES TIMEOUT 60)

    # The program that asks GitHub for the release list, against a server on the loopback address.
    add_test(NAME test_updatetransport_https_cases COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_updatetransport_https_cases.py $<TARGET_FILE:test_updatetransport_https>)
    set_tests_properties(test_updatetransport_https_cases PROPERTIES TIMEOUT 300)
    # The request to GitHub needs internet access, so no test run includes it unless the configuration asks for it.
    # The program returns 77 when GitHub answers with its rate limit or an outage, which CTest shows as skipped.
    if(EGTRAIN_NETWORK_TESTS)
        add_test(NAME test_updatetransport_https COMMAND test_updatetransport_https)
        set_tests_properties(test_updatetransport_https PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endif()

    # SelfUpdater is compiled here with its test hook, which points it at a server on the loopback
    # address and at folders of the test. The update probe stands in for the helper.
    add_executable(test_selfupdater
        ${SRC_DIR}/tests/test_selfupdater.cpp
        ${SRC_DIR}/update/SelfUpdater.cpp)
    target_compile_definitions(test_selfupdater PRIVATE EGTRAIN_UPDATE_TEST_HOOK=1)
    target_link_libraries(test_selfupdater PRIVATE egtrain_update Qt5::Core Qt5::Network)
    add_test(NAME test_selfupdater COMMAND test_selfupdater $<TARGET_FILE:egtrain_update_probe>)
    set_tests_properties(test_selfupdater PROPERTIES TIMEOUT 120)

    add_executable(test_updatestaging
        ${SRC_DIR}/tests/test_updatestaging.cpp)
    target_link_libraries(test_updatestaging PRIVATE Qt5::Core)
    add_test(NAME test_updatestaging COMMAND test_updatestaging)

    add_executable(test_updaterecord ${SRC_DIR}/tests/test_updaterecord.cpp)
    target_link_libraries(test_updaterecord PRIVATE egtrain_update Qt5::Core)
    add_test(NAME test_updaterecord COMMAND test_updaterecord)

    add_executable(test_distribution ${SRC_DIR}/tests/test_distribution.cpp)
    target_link_libraries(test_distribution PRIVATE egtrain_update Qt5::Core)
    add_test(NAME test_distribution COMMAND test_distribution)

    add_executable(test_recoverystore ${SRC_DIR}/tests/test_recoverystore.cpp)
    target_link_libraries(test_recoverystore PRIVATE egtrain_recovery Qt5::Core)
    add_test(NAME test_recoverystore COMMAND test_recoverystore)
    # The same program, started again as the child that holds or loses a copy.
    add_test(NAME test_recoverystore_process COMMAND test_recoverystore --processes)
    set_tests_properties(test_recoverystore_process PROPERTIES TIMEOUT 120)

    add_executable(test_recoverycontroller ${SRC_DIR}/tests/test_recoverycontroller.cpp)
    target_link_libraries(test_recoverycontroller PRIVATE egtrain_recovery Qt5::Core)
    add_test(NAME test_recoverycontroller COMMAND test_recoverycontroller)
    set_tests_properties(test_recoverycontroller PROPERTIES TIMEOUT 120)

    add_executable(test_recoveryprompt ${SRC_DIR}/tests/test_recoveryprompt.cpp)
    target_link_libraries(test_recoveryprompt PRIVATE egtrain_recovery Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_recoveryprompt COMMAND test_recoveryprompt)
    set_tests_properties(test_recoveryprompt PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_guisimulationsnapshot
        ${SRC_DIR}/tests/test_guisimulationsnapshot.cpp)
    target_link_libraries(test_guisimulationsnapshot PRIVATE egtrain_dispatch)
    target_include_directories(test_guisimulationsnapshot PRIVATE ${SRC_DIR})
    add_test(NAME test_guisimulationsnapshot COMMAND test_guisimulationsnapshot)

    # Runs the line fixture in this process and compares the live frames of its snapshots with the golden files.
    add_executable(test_liveframe ${SRC_DIR}/tests/test_liveframe.cpp)
    target_link_libraries(test_liveframe PRIVATE egtrain_live egtrain_dispatch egtrain_rttp egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_liveframe
        COMMAND test_liveframe
            --fixture ${SRC_DIR}/tests/fixtures/scenes/line
            --expect ${SRC_DIR}/tests/live/expected
            --schema ${CMAKE_CURRENT_SOURCE_DIR}/docs/live/frame-v1.schema.json)
    set_tests_properties(test_liveframe PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

    # Checks the requests of the command socket and the event frame that reports a command. No socket, thread or run.
    add_executable(test_livecommand ${SRC_DIR}/tests/test_livecommand.cpp)
    target_link_libraries(test_livecommand PRIVATE egtrain_live nlohmann_json::nlohmann_json)
    add_test(NAME test_livecommand COMMAND test_livecommand)
    set_tests_properties(test_livecommand PROPERTIES TIMEOUT 120)

    # Replays the commands of a live client against the line fixture in this process, and compares the results with
    # those of a run without commands.
    add_executable(test_livereplay ${SRC_DIR}/tests/test_livereplay.cpp)
    target_link_libraries(test_livereplay PRIVATE egtrain_live egtrain_dispatch egtrain_rttp egtrain_sim cppzmq Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_livereplay
        COMMAND test_livereplay
            --fixture ${SRC_DIR}/tests/fixtures/scenes/line
            --script ${SRC_DIR}/tests/live/commands)
    set_tests_properties(test_livereplay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

    # Starts the live server in this process and talks to it through ZeroMQ sockets on the loopback address.
    add_executable(test_liveserver ${SRC_DIR}/tests/test_liveserver.cpp)
    target_link_libraries(test_liveserver PRIVATE egtrain_live cppzmq nlohmann_json::nlohmann_json Threads::Threads)
    add_test(NAME test_liveserver COMMAND test_liveserver)
    set_tests_properties(test_liveserver PROPERTIES TIMEOUT 120)

    # The small client of the live stream. test_live_stream_smoke runs it beside the application.
    add_executable(live_client ${SRC_DIR}/tests/live_client.cpp)
    target_link_libraries(live_client PRIVATE cppzmq nlohmann_json::nlohmann_json)

    add_executable(test_followavailability ${SRC_DIR}/tests/test_followavailability.cpp)
    target_link_libraries(test_followavailability PRIVATE egtrain_util)
    target_include_directories(test_followavailability PRIVATE ${SRC_DIR})
    add_test(NAME test_followavailability COMMAND test_followavailability)

    add_executable(test_replaycoverage ${SRC_DIR}/tests/test_replaycoverage.cpp)
    target_link_libraries(test_replaycoverage PRIVATE egtrain_dispatch egtrain_util)
    target_include_directories(test_replaycoverage PRIVATE ${SRC_DIR})
    add_test(NAME test_replaycoverage COMMAND test_replaycoverage)

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

    add_executable(test_delayanalysis ${SRC_DIR}/tests/test_delayanalysis.cpp)
    target_link_libraries(test_delayanalysis PRIVATE egtrain_diagrams)
    add_test(NAME test_delayanalysis COMMAND test_delayanalysis)

    add_executable(test_delayanalysiswindow ${SRC_DIR}/tests/test_delayanalysiswindow.cpp)
    target_link_libraries(test_delayanalysiswindow PRIVATE egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt5::Network Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_delayanalysiswindow COMMAND test_delayanalysiswindow)
    set_tests_properties(test_delayanalysiswindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_simulationworker ${SRC_DIR}/tests/test_simulationworker.cpp)
    target_link_libraries(test_simulationworker PRIVATE egtrain_dispatch egtrain_rttp egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
    add_test(NAME test_simulationworker COMMAND test_simulationworker ${SRC_DIR}/Scenes/Assignment_Gvc_Gdg_Ut)

    add_executable(test_blockingtimediagram ${SRC_DIR}/tests/test_blockingtimediagram.cpp)
    target_link_libraries(test_blockingtimediagram PRIVATE egtrain_diagrams)
    add_test(NAME test_blockingtimediagram COMMAND test_blockingtimediagram)

    add_executable(test_capacityanalysis ${SRC_DIR}/tests/test_capacityanalysis.cpp)
    target_link_libraries(test_capacityanalysis PRIVATE egtrain_diagrams)
    add_test(NAME test_capacityanalysis COMMAND test_capacityanalysis)

    add_executable(test_areacapacity ${SRC_DIR}/tests/test_areacapacity.cpp)
    target_link_libraries(test_areacapacity PRIVATE egtrain_diagrams)
    add_test(NAME test_areacapacity COMMAND test_areacapacity)

    add_executable(test_areacapacitywindow ${SRC_DIR}/tests/test_areacapacitywindow.cpp)
    target_link_libraries(test_areacapacitywindow PRIVATE egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt5::Network Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_areacapacitywindow COMMAND test_areacapacitywindow)
    set_tests_properties(test_areacapacitywindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

    add_executable(test_networkpresenter ${SRC_DIR}/tests/test_networkpresenter.cpp)
    target_link_libraries(test_networkpresenter PRIVATE egtrain_graphics Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_networkpresenter COMMAND test_networkpresenter)
    set_tests_properties(test_networkpresenter PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_runtimecanvas ${SRC_DIR}/tests/test_runtimecanvas.cpp)
    target_link_libraries(test_runtimecanvas PRIVATE egtrain_graphics egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_runtimecanvas COMMAND test_runtimecanvas)
    set_tests_properties(test_runtimecanvas PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_followcamera ${SRC_DIR}/tests/test_followcamera.cpp)
    target_include_directories(test_followcamera PRIVATE ${SRC_DIR})
    target_link_libraries(test_followcamera PRIVATE egtrain_graphics egtrain_util Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_followcamera COMMAND test_followcamera)
    set_tests_properties(test_followcamera PROPERTIES
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

    add_executable(test_casechooserdialog ${SRC_DIR}/tests/test_casechooserdialog.cpp)
    target_link_libraries(test_casechooserdialog PRIVATE egtrain_widgets Qt5::Core Qt5::Gui Qt5::Widgets)
    target_compile_definitions(test_casechooserdialog PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_casechooserdialog COMMAND test_casechooserdialog)
    set_tests_properties(test_casechooserdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_choicecombo
        ${SRC_DIR}/tests/test_choicecombo.cpp)
    target_link_libraries(test_choicecombo PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    target_compile_definitions(test_choicecombo PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_choicecombo COMMAND test_choicecombo)
    set_tests_properties(test_choicecombo PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_userprompts ${SRC_DIR}/tests/test_userprompts.cpp)
    target_link_libraries(test_userprompts PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_userprompts COMMAND test_userprompts)
    # The test answers real message boxes. Qt 5.15 crashes when a QMessageBox is shown on the offscreen platform
    # on Windows, so the test uses the native platform there.
    if(WIN32)
        set_tests_properties(test_userprompts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    else()
        set_tests_properties(test_userprompts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    add_executable(test_editor_casesettings ${SRC_DIR}/tests/test_editor_casesettings.cpp)
    target_link_libraries(test_editor_casesettings PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_casesettings COMMAND test_editor_casesettings)
    set_tests_properties(test_editor_casesettings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_composition ${SRC_DIR}/tests/test_editor_composition.cpp)
    target_link_libraries(test_editor_composition PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_composition COMMAND test_editor_composition)
    set_tests_properties(test_editor_composition PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_trainunit ${SRC_DIR}/tests/test_editor_trainunit.cpp)
    target_link_libraries(test_editor_trainunit PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_trainunit COMMAND test_editor_trainunit)
    set_tests_properties(test_editor_trainunit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_trainunitsourcelinker ${SRC_DIR}/tests/test_trainunitsourcelinker.cpp)
    target_link_libraries(test_trainunitsourcelinker PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_trainunitsourcelinker COMMAND test_trainunitsourcelinker)
    # The test answers the real question of the linker. Qt 5.15 crashes when a QMessageBox is shown on the offscreen
    # platform on Windows, so the test uses the native platform there.
    if(WIN32)
        set_tests_properties(test_trainunitsourcelinker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    else()
        set_tests_properties(test_trainunitsourcelinker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    add_executable(test_editor_passenger ${SRC_DIR}/tests/test_editor_passenger.cpp)
    target_link_libraries(test_editor_passenger PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_passenger COMMAND test_editor_passenger)
    set_tests_properties(test_editor_passenger PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_service ${SRC_DIR}/tests/test_editor_service.cpp)
    target_link_libraries(test_editor_service PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_service COMMAND test_editor_service)
    set_tests_properties(test_editor_service PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_timetablestops ${SRC_DIR}/tests/test_editor_timetablestops.cpp)
    target_link_libraries(test_editor_timetablestops PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_timetablestops COMMAND test_editor_timetablestops)
    set_tests_properties(test_editor_timetablestops PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_scenario ${SRC_DIR}/tests/test_editor_scenario.cpp)
    target_link_libraries(test_editor_scenario PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_scenario COMMAND test_editor_scenario)
    set_tests_properties(test_editor_scenario PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_incident ${SRC_DIR}/tests/test_editor_incident.cpp)
    target_link_libraries(test_editor_incident PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_incident COMMAND test_editor_incident)
    set_tests_properties(test_editor_incident PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_entrancedelay ${SRC_DIR}/tests/test_editor_entrancedelay.cpp)
    target_link_libraries(test_editor_entrancedelay PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_entrancedelay COMMAND test_editor_entrancedelay)
    set_tests_properties(test_editor_entrancedelay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_infrastructure ${SRC_DIR}/tests/test_editor_infrastructure.cpp)
    target_link_libraries(test_editor_infrastructure PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_infrastructure COMMAND test_editor_infrastructure)
    set_tests_properties(test_editor_infrastructure PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editor_infrastructure_topology ${SRC_DIR}/tests/test_editor_infrastructure_topology.cpp)
    target_link_libraries(test_editor_infrastructure_topology PRIVATE egtrain_editors Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_editor_infrastructure_topology COMMAND test_editor_infrastructure_topology)
    set_tests_properties(test_editor_infrastructure_topology PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_editorlayout ${SRC_DIR}/tests/test_editorlayout.cpp)
    target_link_libraries(test_editorlayout PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    target_compile_definitions(test_editorlayout PRIVATE
        EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss")
    add_test(NAME test_editorlayout COMMAND test_editorlayout)
    set_tests_properties(test_editorlayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_elidedlabel ${SRC_DIR}/tests/test_elidedlabel.cpp)
    target_link_libraries(test_elidedlabel PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_elidedlabel COMMAND test_elidedlabel)
    set_tests_properties(test_elidedlabel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

    add_executable(test_scenereferences ${SRC_DIR}/tests/test_scenereferences.cpp)
    target_link_libraries(test_scenereferences PRIVATE egtrain_scene)
    add_test(NAME test_scenereferences COMMAND test_scenereferences)

    add_executable(test_scenedocument ${SRC_DIR}/tests/test_scenedocument.cpp)
    target_link_libraries(test_scenedocument PRIVATE egtrain_editors)
    add_test(NAME test_scenedocument COMMAND test_scenedocument)

    add_executable(test_runselection ${SRC_DIR}/tests/test_runselection.cpp)
    target_link_libraries(test_runselection PRIVATE egtrain_runselection)
    add_test(NAME test_runselection COMMAND test_runselection)

    add_executable(test_sceneimporter ${SRC_DIR}/tests/test_sceneimporter.cpp)
    target_link_libraries(test_sceneimporter PRIVATE egtrain_scene)
	add_test(NAME test_sceneimporter COMMAND test_sceneimporter)

	add_executable(test_scenesavepolicy ${SRC_DIR}/tests/test_scenesavepolicy.cpp)
	target_link_libraries(test_scenesavepolicy PRIVATE Qt5::Core)
	add_test(NAME test_scenesavepolicy COMMAND test_scenesavepolicy)

	add_executable(test_dialogfolders ${SRC_DIR}/tests/test_dialogfolders.cpp)
	target_link_libraries(test_dialogfolders PRIVATE Qt5::Core)
	add_test(NAME test_dialogfolders COMMAND test_dialogfolders)

	add_executable(test_cannotopenmessage ${SRC_DIR}/tests/test_cannotopenmessage.cpp)
	target_link_libraries(test_cannotopenmessage PRIVATE Qt5::Core)
	add_test(NAME test_cannotopenmessage COMMAND test_cannotopenmessage)

	add_executable(test_releasenotes ${SRC_DIR}/tests/test_releasenotes.cpp)
	target_link_libraries(test_releasenotes PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
	add_test(NAME test_releasenotes COMMAND test_releasenotes)
	set_tests_properties(test_releasenotes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

	add_executable(test_scenewriter ${SRC_DIR}/tests/test_scenewriter.cpp)
	target_link_libraries(test_scenewriter PRIVATE egtrain_scene)
	add_test(NAME test_scenewriter
		COMMAND test_scenewriter ${SRC_DIR}/Scenes)

	add_executable(test_sceneexporter ${SRC_DIR}/tests/test_sceneexporter.cpp)
	target_link_libraries(test_sceneexporter PRIVATE egtrain_scene)
	target_compile_definitions(test_sceneexporter PRIVATE EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests")
	add_test(NAME test_sceneexporter COMMAND test_sceneexporter)

	add_executable(test_scenebundle ${SRC_DIR}/tests/test_scenebundle.cpp)
	target_link_libraries(test_scenebundle PRIVATE egtrain_scene)
	add_test(NAME test_scenebundle
		COMMAND test_scenebundle ${SRC_DIR}/tests/fixtures/scenes/minimal)

	add_executable(test_scenehistory ${SRC_DIR}/tests/test_scenehistory.cpp)
	target_link_libraries(test_scenehistory PRIVATE egtrain_scene)
	add_test(NAME test_scenehistory COMMAND test_scenehistory ${SRC_DIR}/Scenes)

	add_executable(test_sceneopenrequest ${SRC_DIR}/tests/test_sceneopenrequest.cpp)
	target_link_libraries(test_sceneopenrequest PRIVATE egtrain_sceneopen Qt5::Core)
	add_test(NAME test_sceneopenrequest COMMAND test_sceneopenrequest)

	# The test starts copies of itself as the second process.
	add_executable(test_openrequestrelay ${SRC_DIR}/tests/test_openrequestrelay.cpp)
	target_link_libraries(test_openrequestrelay PRIVATE egtrain_openrelay Qt5::Core Qt5::Network)
	add_test(NAME test_openrequestrelay COMMAND test_openrequestrelay)
	set_tests_properties(test_openrequestrelay PROPERTIES TIMEOUT 120)

	add_executable(test_scenecompatibility ${SRC_DIR}/tests/test_scenecompatibility.cpp)
	target_link_libraries(test_scenecompatibility PRIVATE egtrain_scene)
	add_test(NAME test_scenecompatibility
		COMMAND test_scenecompatibility ${SRC_DIR}/tests/fixtures/scenes/minimal)

	add_executable(test_sceneversions ${SRC_DIR}/tests/test_sceneversions.cpp)
	target_link_libraries(test_sceneversions PRIVATE egtrain_scene)
	add_test(NAME test_sceneversions
		COMMAND test_sceneversions ${SRC_DIR}/tests/fixtures/scene-versions)

	add_executable(test_scenebuilder ${SRC_DIR}/tests/test_scenebuilder.cpp)
	target_link_libraries(test_scenebuilder PRIVATE egtrain_sim)
	add_test(NAME test_scenebuilder COMMAND test_scenebuilder)

	add_executable(test_stopdwell ${SRC_DIR}/tests/test_stopdwell.cpp)
	target_link_libraries(test_stopdwell PRIVATE egtrain_sim)
	add_test(NAME test_stopdwell COMMAND test_stopdwell ${SRC_DIR}/tests/fixtures/scenes/crossovers)

	add_executable(test_operationsbuilder ${SRC_DIR}/tests/test_operationsbuilder.cpp)
	target_link_libraries(test_operationsbuilder PRIVATE egtrain_diagrams egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
	target_compile_definitions(test_operationsbuilder PRIVATE EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests")
	add_test(NAME test_operationsbuilder COMMAND test_operationsbuilder)

	# Runs two committed scenes in this process, like the characterization tests.
	add_executable(test_completedrun ${SRC_DIR}/tests/test_completedrun.cpp)
	target_link_libraries(test_completedrun PRIVATE egtrain_dispatch egtrain_diagrams egtrain_telemetry egtrain_rttp egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
	add_test(NAME test_completedrun
		COMMAND test_completedrun ${SRC_DIR}/Scenes/Assignment_Gvc_Gdg_Ut ${SRC_DIR}/Scenes/Paimpol)
	# The capacity dialog test closes two message boxes. Qt 5.15 crashes when a QMessageBox is shown on
	# the offscreen platform on Windows, so the test uses the native platform there.
	if(WIN32)
		set_tests_properties(test_completedrun PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 120)
	else()
		set_tests_properties(test_completedrun PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
	endif()

	# Runs a line fixture and Paimpol in this process, in the order that the test explains.
	add_executable(test_simulationrun ${SRC_DIR}/tests/test_simulationrun.cpp)
	target_link_libraries(test_simulationrun PRIVATE egtrain_dispatch egtrain_rttp egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets)
	add_test(NAME test_simulationrun
		COMMAND test_simulationrun ${SRC_DIR}/tests/fixtures/scenes/line ${SRC_DIR}/Scenes/Paimpol)
	set_tests_properties(test_simulationrun PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

	# Behaviour characterization of the simulation core (see tests/characterization).
	add_subdirectory(EGTRAIN/QEGTRAIN/tests/characterization)

	# Scripted GUI runs, compiled into QEGTRAIN when tests are built.
	add_subdirectory(EGTRAIN/QEGTRAIN/tests/e2e)

	add_executable(test_rttp ${SRC_DIR}/tests/test_rttp.cpp)
	# The test defines initial_variables, so egtrain_sim's Infrastructure.cpp member must stay out of the link.
	target_link_libraries(test_rttp PRIVATE egtrain_rttp egtrain_sim cppzmq nlohmann_json::nlohmann_json)
	add_test(NAME test_rttp COMMAND test_rttp)
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
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/startup_launch_contract.py $<TARGET_FILE:QEGTRAIN> $<TARGET_FILE:scene_tool>)
	# Cover two 60-second child launches, two 8-second PTY checks, a 60-second wait for the window
	# launch with a file name and cleanup, plus 30 seconds for the other file name launches. The
	# launches that hand a file to a running window add 60 seconds for the window to start, 60 seconds
	# for a file to arrive and 60 seconds for a scripted launch.
	set_tests_properties(test_startup_launch_contract PROPERTIES TIMEOUT 480)
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
	add_test(NAME test_output_folder
		COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Paimpol)
	set_tests_properties(test_output_folder PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QEGTRAIN_E2E_OUTPUT_FOLDER=1;QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/output-folder-test"
		PASS_REGULAR_EXPRESSION "E2E_OUTPUT_FOLDER_OK"
		TIMEOUT 120)
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
	# The case in which a second run follows the first in one process uses a scripted run of the window, which this repository
	# checks on macOS only (see the visual polish smokes below).
	if(APPLE)
		set(egtrain_live_stream_smoke_args --second-run)
	endif()
	add_test(NAME test_live_stream_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/live_stream_smoke.py
			$<TARGET_FILE:QEGTRAIN> $<TARGET_FILE:live_client> ${egtrain_live_stream_smoke_args})
	set_tests_properties(test_live_stream_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
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
		add_test(NAME test_editor_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/editor_smoke.sh)
		set_tests_properties(test_editor_smoke PROPERTIES TIMEOUT 300)
		add_test(NAME test_dialog_presentation_contract
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/creator_acceptance_smoke.sh --dialog-contract)
		set_tests_properties(test_dialog_presentation_contract PROPERTIES TIMEOUT 600)
		# The scripts get the built application, scene_tool and a platform plugin that needs no display.
		set_tests_properties(test_csv_export_smoke test_lebanon_scene_smoke test_creator_acceptance_smoke test_editor_smoke test_dialog_presentation_contract
			PROPERTIES ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>;QEGTRAIN_SCENE_TOOL=$<TARGET_FILE:scene_tool>;QT_QPA_PLATFORM=offscreen")
	else()
		message(STATUS "Windows: skipping bash smoke tests (test_csv_export_smoke, test_lebanon_scene_smoke, test_creator_acceptance_smoke, test_editor_smoke, test_dialog_presentation_contract)")
	endif()
	# The scripted scenarios of the release workflow and the route diagram run. A build with sanitizers
	# does not register them: the sanitizer job of the release workflow runs all of CTest on an
	# instrumented Debug build, and the time limits of these scripts are set for an uninstrumented one.
	if(NOT EGTRAIN_ENABLE_SANITIZERS)
		if(UNIX)
			add_test(NAME test_scene_render_smoke
				COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/scene_render_smoke.sh)
			set_tests_properties(test_scene_render_smoke PROPERTIES TIMEOUT 300)
			add_test(NAME test_track_preview_smoke
				COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/track_preview_smoke.sh)
			set_tests_properties(test_track_preview_smoke PROPERTIES TIMEOUT 240)
			add_test(NAME test_legacy_import_smoke
				COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/legacy_import_smoke.sh)
			set_tests_properties(test_legacy_import_smoke PROPERTIES TIMEOUT 120)
			add_test(NAME test_route_diagram_smoke
				COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/route_diagram_smoke.py
					$<TARGET_FILE:QEGTRAIN> $<TARGET_FILE_DIR:test_diagramwindow>)
			set_tests_properties(test_route_diagram_smoke PROPERTIES TIMEOUT 480)
			# The scripts get the built application and a platform plugin that needs no display.
			set_tests_properties(test_scene_render_smoke test_track_preview_smoke test_legacy_import_smoke
				PROPERTIES ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>;QT_QPA_PLATFORM=offscreen")
		else()
			message(STATUS "Windows: skipping the GUI smoke tests (test_scene_render_smoke, test_track_preview_smoke, test_legacy_import_smoke, test_route_diagram_smoke)")
		endif()
		if(APPLE)
			# visual_polish_smoke.sh runs its phases one after another when it gets no argument. The phases write file
			# names of their own below TMPDIR, so CTest runs them as separate tests side by side. The last phase, which
			# runs scene_render_smoke.sh and legacy_import_smoke.sh, has no test of its own: those two are tests above.
			# The cost is about the seconds that the test takes on a development machine, so that the longest start first.
			macro(egtrain_add_visual_polish_smoke name cost timeout)
				add_test(NAME test_visual_polish_smoke_${name}
					COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/visual_polish_smoke.sh ${ARGN})
				set_tests_properties(test_visual_polish_smoke_${name} PROPERTIES
					ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>;QT_QPA_PLATFORM=offscreen"
					TIMEOUT ${timeout}
					COST ${cost})
				list(APPEND egtrain_visual_polish_smokes test_visual_polish_smoke_${name})
			endmacro()
			egtrain_add_visual_polish_smoke(replay_copenhagen 100 600 replay-copenhagen)
			egtrain_add_visual_polish_smoke(replay 45 300 completion replay discard)
			egtrain_add_visual_polish_smoke(signals 45 300 signals)
			egtrain_add_visual_polish_smoke(live 35 300 dpr1 color dpr2)
			egtrain_add_visual_polish_smoke(stations 30 240 stations)
		endif()
	endif()
	if(APPLE)
		add_test(NAME test_package_contents_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/package_contents_smoke.sh)
		set_tests_properties(test_package_contents_smoke PROPERTIES TIMEOUT 420)
		# Signs a fixture bundle with the release signing script. It needs cc and codesign, not the built application.
		add_test(NAME test_sign_macos
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_sign_macos.sh)
		set_tests_properties(test_sign_macos PROPERTIES TIMEOUT 120)
	endif()
	add_test(NAME test_release_version
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_version.py)
	add_test(NAME test_release_assets
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_release_assets.py)
	add_test(NAME test_make_dmg
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_make_dmg.py)
	add_test(NAME test_installer_script
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_installer_script.py)
	add_test(NAME test_ci_workflow
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_ci_workflow.py)
	add_test(NAME test_signalling_bounds
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_signalling_bounds.py)
	add_test(NAME test_logging_contract
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_logging_contract.py)
	add_test(NAME test_no_e2e_in_product
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_no_e2e_in_product.py)
	add_test(NAME test_diagrams_no_sim_globals
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_diagrams_no_sim_globals.py)
	add_test(NAME test_scene_codec_contract
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_scene_codec_contract.py)
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

	# The same two runs on each scene of tests/fixtures/scene-versions that is meant to run.
	add_test(NAME test_scene_version_run_main-82.1
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
			$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/main-82.1-Paimpol.egscene 300)
	add_test(NAME test_scene_version_run_production-107.1
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
			$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/production-107.1-Paimpol.egscene 300)
	add_test(NAME test_scene_version_run_v1.0.2
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
			$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/v1.0.2-Paimpol.egscene 300)
	add_test(NAME test_scene_version_run_main-81.1
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
			$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/main-81.1-Assignment_Gvc_Gdg_Ut 600)
	set_tests_properties(test_scene_version_run_main-82.1 test_scene_version_run_production-107.1
		test_scene_version_run_v1.0.2 test_scene_version_run_main-81.1 PROPERTIES TIMEOUT 150)

	# A Windows EXE image near 2 GiB fails to start, and fixed-size static storage is what makes an
	# image large. The limit is checked on every Windows build, so such storage is noticed early.
	# The stack reserve is checked with it: the application has its own, scene_tool the linker default.
	if(WIN32)
		set(EGTRAIN_MAX_PE_IMAGE_BYTES 100000000 CACHE STRING
			"Fail test_windows_image_size when QEGTRAIN.exe SizeOfImage exceeds this many bytes")
		add_test(NAME test_windows_image_size
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/pe_image_size.py
				$<TARGET_FILE:QEGTRAIN> --max-bytes ${EGTRAIN_MAX_PE_IMAGE_BYTES}
				--machine x64 --subsystem windows --stack-reserve ${EGTRAIN_STACK_RESERVE_BYTES})
		add_test(NAME test_windows_default_stack
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/pe_image_size.py
				$<TARGET_FILE:scene_tool> --max-bytes ${EGTRAIN_MAX_PE_IMAGE_BYTES}
				--machine x64 --subsystem console --stack-reserve 1048576)
	endif()
	add_test(NAME test_pe_image_size
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_pe_image_size.py)
	add_test(NAME test_pe_imports
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_pe_imports.py)
	# The signature check runs on copies of scene_tool.exe that a throwaway self-signed certificate signs.
	if(WIN32)
		find_program(EGTRAIN_POWERSHELL NAMES pwsh powershell)
		if(EGTRAIN_POWERSHELL)
			add_test(NAME test_verify_signatures
				COMMAND ${EGTRAIN_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
					-File ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_verify_signatures.ps1
					-SceneTool $<TARGET_FILE:scene_tool>)
			set_tests_properties(test_verify_signatures PROPERTIES TIMEOUT 300)
		else()
			message(STATUS "Windows: skipping test_verify_signatures (no PowerShell found)")
		endif()
	endif()
	add_test(NAME test_package_start_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_package_start_smoke.py)
	add_test(NAME test_linux_rehearsal
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_linux_rehearsal.py)
	# A 32-bit configure stops with the message that names the supported platform. A Ninja build tree checks this with
	# the default generator, which is Visual Studio on Windows.
	if(WIN32)
		if(CMAKE_GENERATOR MATCHES "Visual Studio")
			set(egtrain_win32_generator -G ${CMAKE_GENERATOR})
		else()
			set(egtrain_win32_generator)
		endif()
		add_test(NAME test_win32_configure_rejected
			COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}
				-B ${CMAKE_CURRENT_BINARY_DIR}/win32-configure-check ${egtrain_win32_generator} -A Win32)
		set_tests_properties(test_win32_configure_rejected PROPERTIES
			PASS_REGULAR_EXPRESSION "EGTRAIN supports 64-bit Windows"
			TIMEOUT 300)
	endif()

	# Test executables that use only strict libraries are strict as well.
	foreach(egtrain_test
			test_timeformat test_version test_log test_speedformat test_trajectory test_csvwriter
			test_playbackprofiler
			test_httptransport test_selfupdater
			test_telemetryconsent test_telemetryqueue test_telemetrysender test_telemetryoperations
			test_telemetrynetwork test_updatechecker test_updatestaging test_updaterecord test_recoverystore
			test_distribution
			test_recoverycontroller test_recoveryprompt test_trackpreview test_routestations test_scenevalidator test_stopinsertion
			test_scenereferences test_scenedocument test_runselection
			test_sceneimporter test_scenesavepolicy test_dialogfolders test_cannotopenmessage test_releasenotes test_scenewriter test_sceneexporter test_scenebundle test_scenehistory
			test_scenecompatibility test_sceneopenrequest test_openrequestrelay
			test_sceneversions
			test_delayanalysis test_delayanalysiswindow test_areacapacity test_areacapacitywindow test_blockingtimediagram test_capacityanalysis test_routediagramcoordinates
			test_diagramwindow test_timetabletablewindow test_routereferencechoice test_tractioncurve
			test_visualpolish test_networklegend test_stationoverlay test_networkscene_selection
			test_networkpresenter test_networkview test_compactdoublespinbox test_colorchoicebutton test_aboutdialog
			test_dialoglayout test_casechooserdialog test_choicecombo test_editorlayout test_userprompts test_editor_casesettings test_editor_passenger test_editor_service test_editor_timetablestops test_editor_scenario test_editor_incident test_editor_entrancedelay test_editor_infrastructure test_editor_infrastructure_topology test_editor_composition test_editor_trainunit test_trainunitsourcelinker test_elidedlabel test_timeprogressbar test_highlighteffect
			test_consolestream test_qt_resource_images test_followavailability test_followcamera test_replaycoverage test_livecommand test_liveserver live_client)
		egtrain_target_warnings(${egtrain_test} STRICT)
	endforeach()

	# Canonical V1 is the product path. Legacy tests cover only converter and
	# compatibility boundaries and can be selected with `ctest -L legacy-compat`.
	set_tests_properties(test_scenevalidator test_routestations test_stopinsertion test_scenereferences test_scenesavepolicy test_scenewriter test_scenebuilder test_stopdwell test_operationsbuilder test_scene_tool_diagnostics test_trackpreview test_scene_codec_contract
		PROPERTIES LABELS "scene-v1")
	set_tests_properties(test_sceneimporter test_sceneexporter test_headless_smoke_decode
		PROPERTIES LABELS "legacy-compat")
	set_tests_properties(test_scenebundle test_scenecompatibility test_sceneversions PROPERTIES LABELS "scene-v2")

	# Every test is either `unit` (in-process or a static check) or `integration`
	# (starts or inspects a built program, or talks to a loopback server).
	# `gui` needs a Qt platform plugin and `slow` takes more than 20 s locally. `network` needs internet access.
	# APPEND keeps the scene labels above. Names not registered on this platform are skipped.
	function(egtrain_label_tests label)
		foreach(name IN LISTS ARGN)
			if(TEST ${name})
				set_property(TEST ${name} APPEND PROPERTY LABELS ${label})
			endif()
		endforeach()
	endfunction()
	egtrain_label_tests(unit
		test_updatepreparation test_timeformat test_version test_log test_telemetryconsent test_telemetryqueue
		test_telemetrysender test_telemetryoperations test_updatechecker test_updatestaging test_updaterecord test_recoverystore
		test_distribution
		test_guisimulationsnapshot test_liveframe test_livecommand test_liveserver test_followavailability test_replaycoverage test_simulationworker test_recoverycontroller test_recoveryprompt test_trackpreview test_speedformat test_trajectory test_runresults test_delayanalysis test_delayanalysiswindow
		test_areacapacity test_areacapacitywindow test_blockingtimediagram test_capacityanalysis test_routediagramcoordinates test_diagramwindow
		test_routestations test_routereferencechoice
		test_timetabletablewindow test_visualpolish test_networklegend test_stationoverlay
		test_networkscene_selection test_networkpresenter test_runtimecanvas test_playbackprofiler test_playback_profile test_networkview test_followcamera
		test_compactdoublespinbox test_colorchoicebutton test_aboutdialog test_dialoglayout test_casechooserdialog test_choicecombo test_editorlayout test_userprompts test_editor_casesettings test_editor_passenger test_editor_service test_editor_timetablestops test_editor_scenario test_editor_incident test_editor_entrancedelay test_editor_infrastructure test_editor_infrastructure_topology test_editor_composition test_editor_trainunit test_trainunitsourcelinker test_elidedlabel test_timeprogressbar
		test_highlighteffect test_csvwriter test_tractioncurve test_consolestream test_qt_resources
		test_app_icon_config test_qt_resource_images test_scenevalidator test_stopinsertion test_scenereferences test_scenedocument test_runselection test_sceneimporter
		test_scenesavepolicy test_dialogfolders test_cannotopenmessage test_releasenotes test_scenewriter test_sceneexporter test_scenebundle test_scenehistory test_scenecompatibility test_sceneopenrequest test_scenebuilder test_stopdwell
		test_sceneversions
		test_operationsbuilder test_completedrun test_simulationrun test_rttp test_headless_smoke_decode test_release_version test_release_assets
		test_ci_workflow test_signalling_bounds test_logging_contract test_diagrams_no_sim_globals test_no_e2e_in_product test_scene_codec_contract test_windows_subsystem_config test_raii_contract
		test_installer_script
		test_ownership_inventory test_measure_peak_rss test_pe_image_size test_pe_imports test_package_start_smoke test_linux_rehearsal test_make_dmg)
	egtrain_label_tests(integration
		test_telemetrynetwork_reject test_telemetrynetwork_trusted test_updatehelper test_recoverystore_process test_openrequestrelay
		test_httptransport test_selfupdater test_updatetransport_https test_updatetransport_https_cases
		test_route_choice_missing_data test_external_sharing_no_listener test_livereplay test_live_stream_smoke test_scene_tool_diagnostics
		test_startup_launch_contract test_case_chooser_contract test_scene_drop test_output_folder
		test_native_startup_without_legacy test_runtime_memory_safety test_simulation_determinism test_station_departure
		test_gui_autostart_smoke test_close_during_run_smoke test_csv_export_smoke test_lebanon_scene_smoke
		test_creator_acceptance_smoke test_editor_smoke test_dialog_presentation_contract test_package_contents_smoke test_headless_scene_smoke
		test_scene_render_smoke test_track_preview_smoke test_legacy_import_smoke test_route_diagram_smoke ${egtrain_visual_polish_smokes}
		test_scene_version_run_main-82.1 test_scene_version_run_production-107.1 test_scene_version_run_v1.0.2 test_scene_version_run_main-81.1
		test_windows_image_size test_windows_default_stack test_win32_configure_rejected test_verify_signatures
		test_sign_macos)
	egtrain_label_tests(gui
		test_telemetryconsent test_telemetryoperations test_diagramwindow test_timetabletablewindow test_areacapacitywindow test_delayanalysiswindow
		test_visualpolish test_networklegend test_stationoverlay test_networkscene_selection test_networkpresenter test_runtimecanvas
		test_releasenotes test_networkview test_followcamera test_compactdoublespinbox test_colorchoicebutton test_aboutdialog test_dialoglayout test_casechooserdialog test_choicecombo test_editorlayout test_userprompts test_editor_casesettings test_editor_passenger test_editor_service test_editor_timetablestops test_editor_scenario test_editor_incident test_editor_entrancedelay test_editor_infrastructure test_editor_infrastructure_topology test_editor_composition test_editor_trainunit test_trainunitsourcelinker test_elidedlabel
		test_routereferencechoice test_recoveryprompt test_timeprogressbar test_highlighteffect test_qt_resource_images test_scene_drop test_output_folder
		test_startup_launch_contract test_case_chooser_contract test_gui_autostart_smoke test_live_stream_smoke
		test_close_during_run_smoke test_csv_export_smoke test_creator_acceptance_smoke test_editor_smoke test_dialog_presentation_contract
		test_package_contents_smoke test_scene_render_smoke test_track_preview_smoke test_legacy_import_smoke test_route_diagram_smoke
		${egtrain_visual_polish_smokes})
	egtrain_label_tests(slow test_gui_autostart_smoke test_dialog_presentation_contract ${egtrain_visual_polish_smokes})
	egtrain_label_tests(network test_updatetransport_https)

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

	# These tests start the application with its user settings, which hold the list of recent cases.
	# Each gets a settings folder of its own, so that tests can run side by side without sharing
	# or changing the settings of the user.
	foreach(name IN ITEMS test_startup_launch_contract test_scene_drop test_output_folder test_gui_autostart_smoke
			test_close_during_run_smoke test_csv_export_smoke test_scene_render_smoke test_track_preview_smoke
			test_legacy_import_smoke test_route_diagram_smoke ${egtrain_visual_polish_smokes})
		if(TEST ${name})
			set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT
				"QEGTRAIN_E2E_SETTINGS_DIR=${CMAKE_CURRENT_BINARY_DIR}/settings/${name}")
		endif()
	endforeach()

	# Without this, these tests write their run output below the application data directory of the user. Each gets an output folder of its own.
	foreach(name IN ITEMS test_scene_render_smoke test_track_preview_smoke test_legacy_import_smoke test_route_diagram_smoke
			${egtrain_visual_polish_smokes})
		if(TEST ${name})
			set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT
				"QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/output/${name}")
		endif()
	endforeach()

	# The longest tests start first, so that a parallel run does not end waiting for one of them.
	# Without the timings of an earlier run, ctest orders tests by this cost.
	set_tests_properties(test_gui_autostart_smoke PROPERTIES COST 80)

	# A build with sanitizers runs several times slower than the limits above allow. The scale multiplies the limit of
	# every test registered in this file; the characterization tests have limits of their own.
	set(EGTRAIN_TEST_TIMEOUT_SCALE 1 CACHE STRING "Factor for the time limit of the tests registered in the top-level CMakeLists.txt")
	if(NOT EGTRAIN_TEST_TIMEOUT_SCALE MATCHES "^[1-9][0-9]*$")
		message(FATAL_ERROR "EGTRAIN_TEST_TIMEOUT_SCALE must be a positive whole number")
	endif()
	if(EGTRAIN_TEST_TIMEOUT_SCALE GREATER 1)
		foreach(name IN LISTS egtrain_all_tests)
			# Scripts with waits of their own, such as startup_launch_contract.py, scale them by the same factor.
			set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "EGTRAIN_TEST_TIMEOUT_SCALE=${EGTRAIN_TEST_TIMEOUT_SCALE}")
			get_test_property(${name} TIMEOUT egtrain_test_timeout)
			if(egtrain_test_timeout)
				math(EXPR egtrain_test_timeout "${egtrain_test_timeout} * ${EGTRAIN_TEST_TIMEOUT_SCALE}")
				set_tests_properties(${name} PROPERTIES TIMEOUT ${egtrain_test_timeout})
			endif()
		endforeach()
	endif()

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
