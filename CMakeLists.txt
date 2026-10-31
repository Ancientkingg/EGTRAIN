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
# macros, and NOGDI drops the GDI Arc() that clashes with our class Arc.
if(MSVC)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX NOGDI)
    # MainWindow.cpp exceeds the section limit of a standard object file in RelWithDebInfo builds.
    add_compile_options(/bigobj)
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
# ToolsTools defines the qhelpgenerator target that builds the manual.
find_package(Qt6 COMPONENTS Core Gui Widgets Charts Svg Network Help ToolsTools)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR "EGTRAIN needs Qt 6 with the Charts, Svg and Help modules; CI builds with Qt 6.8.3. "
        "Add the folder of the Qt 6 installation to CMAKE_PREFIX_PATH.")
endif()

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
# only the objects it references.
add_library(egtrain_util STATIC
    ${SRC_DIR}/util/CsvWriter.cpp
    ${SRC_DIR}/util/Log.cpp
    ${SRC_DIR}/util/PlaybackProfiler.cpp
    ${SRC_DIR}/util/SpeedFormat.cpp
    ${SRC_DIR}/util/TimeFormat.cpp
    ${SRC_DIR}/util/TrajectoryUtil.cpp
    ${SRC_DIR}/util/Version.cpp)
target_link_libraries(egtrain_util PRIVATE nlohmann_json::nlohmann_json)

# No Qt: scene_tool links this library.
add_library(egtrain_scene STATIC
    ${SRC_DIR}/scene/LoadedDataSummary.cpp
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
    ${SRC_DIR}/scene/SectionReference.cpp
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

# No Qt: the frames of the live contract, built from a GuiSimulationSnapshot, the checks on the requests of its clients,
# and the server that publishes the frames and takes the requests.
add_library(egtrain_live STATIC ${SRC_DIR}/io/LiveCommand.cpp ${SRC_DIR}/io/LiveFrame.cpp ${SRC_DIR}/io/LiveServer.cpp)
target_link_libraries(egtrain_live
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE egtrain_util cppzmq Threads::Threads)

foreach(egtrain_lib egtrain_util egtrain_scene egtrain_sim egtrain_live)
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
    PUBLIC Qt::Core Qt::Gui Qt::Widgets egtrain_scene egtrain_util nlohmann_json::nlohmann_json)

add_library(egtrain_widgets STATIC
    ${SRC_DIR}/widgets/AboutDialog.cpp
    ${SRC_DIR}/widgets/CaseChooserDialog.cpp
    ${SRC_DIR}/widgets/ColorChoiceButton.cpp
    ${SRC_DIR}/widgets/ConsoleWidget.cpp
    ${SRC_DIR}/widgets/FindDialog.cpp
    ${SRC_DIR}/widgets/InfoDockWidget.cpp
    ${SRC_DIR}/widgets/InspectorPanel.cpp
    ${SRC_DIR}/widgets/ManualWindow.cpp
    ${SRC_DIR}/widgets/NetworkLegendWidget.cpp
    ${SRC_DIR}/widgets/TimeProgressBar.cpp)
target_link_libraries(egtrain_widgets
    PUBLIC Qt::Core Qt::Gui Qt::Widgets egtrain_graphics egtrain_util
    PRIVATE Qt::Help)

# The scene document and the panels of the scene editors. SceneEditorPanel.h has no source file, so it is listed here for moc.
add_library(egtrain_editors STATIC
    ${SRC_DIR}/editors/CaseSettingsPanel.cpp
    ${SRC_DIR}/editors/CompositionPanel.cpp
    ${SRC_DIR}/editors/EditorRules.cpp
    ${SRC_DIR}/editors/EntranceDelayPanel.cpp
    ${SRC_DIR}/editors/IncidentPanel.cpp
    ${SRC_DIR}/editors/InfrastructurePanel.cpp
    ${SRC_DIR}/editors/infrastructure/BlockRouteFacets.cpp
    ${SRC_DIR}/editors/infrastructure/InfrastructureGuidance.cpp
    ${SRC_DIR}/editors/infrastructure/SignalFacets.cpp
    ${SRC_DIR}/editors/infrastructure/StationFacets.cpp
    ${SRC_DIR}/editors/infrastructure/TopologyFacets.cpp
    ${SRC_DIR}/editors/PassengerPanel.cpp
    ${SRC_DIR}/editors/SceneDocument.cpp
    ${SRC_DIR}/editors/SceneEditorPanel.h
    ${SRC_DIR}/editors/ScenarioPanel.cpp
    ${SRC_DIR}/editors/ServicePanel.cpp
    ${SRC_DIR}/editors/StopEditorDialog.cpp
    ${SRC_DIR}/editors/TimetableStopsPanel.cpp
    ${SRC_DIR}/editors/TrainUnitPanel.cpp
    ${SRC_DIR}/editors/TrainUnitSourceLinker.cpp
    ${SRC_DIR}/editors/ValidationPanel.cpp)
target_link_libraries(egtrain_editors
    PUBLIC Qt::Core Qt::Gui Qt::Widgets egtrain_scene egtrain_diagrams egtrain_widgets egtrain_runselection egtrain_util)

# The HTTP requests of the update check and the telemetry sender. On Windows they go through WinHTTP, so the
# package needs no TLS plugin of Qt.
add_library(egtrain_http STATIC ${SRC_DIR}/update/HttpTransport.cpp)
target_link_libraries(egtrain_http PUBLIC Qt::Core Qt::Network)
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
target_link_libraries(egtrain_update PUBLIC Qt::Core Qt::Network egtrain_http egtrain_util)

# What the next run uses of the scene apart from the scene itself: the selected scenario and the service
# occurrences that are left out. No Qt; the application and test_runselection link it.
add_library(egtrain_runselection STATIC ${SRC_DIR}/app/RunSelection.cpp)
target_link_libraries(egtrain_runselection PUBLIC egtrain_scene)

# What a command line asks the application to open. Qt Core only; the application and
# test_sceneopenrequest link it.
add_library(egtrain_sceneopen STATIC ${SRC_DIR}/app/SceneOpenRequest.cpp)
target_link_libraries(egtrain_sceneopen PUBLIC Qt::Core)

# Hands a file that the system asked the application to open to the code that opens it: from a second
# process to the running instance, or as an open event. Qt Core, Gui (QFileOpenEvent) and Network
# only; the application and test_openrequestrelay link it.
add_library(egtrain_openrelay STATIC ${SRC_DIR}/app/OpenRequestRelay.cpp)
target_link_libraries(egtrain_openrelay PUBLIC Qt::Core Qt::Gui Qt::Network)

# Recovery copies of a scene with unsaved edits, kept below a folder that the caller names, the
# controller that writes them in the background, and the dialog that offers them. The application and
# the recovery tests link it.
add_library(egtrain_recovery STATIC
    ${SRC_DIR}/recovery/RecoveryController.cpp
    ${SRC_DIR}/recovery/RecoveryPrompt.cpp
    ${SRC_DIR}/recovery/RecoveryStore.cpp)
target_link_libraries(egtrain_recovery PUBLIC Qt::Core Qt::Gui Qt::Widgets egtrain_scene)

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
    target_link_libraries(${egtrain_lib} PUBLIC Qt::Core Qt::Gui Qt::Network Qt::Widgets egtrain_http)
endforeach()

# The application, test_simulationworker and test_characterization run the simulation through these.
add_library(egtrain_dispatch STATIC
    ${SRC_DIR}/app/DispatchController.cpp
    ${SRC_DIR}/app/GuiReplayHistory.cpp
    ${SRC_DIR}/simulation/RunControl.cpp
    ${SRC_DIR}/simulation/SimulationWorker.cpp)
target_link_libraries(egtrain_dispatch
    PUBLIC Qt::Core Qt::Gui Qt::Widgets egtrain_live egtrain_sim)

# DiagramWindow and TimetableTableWindow call telemetry::OperationObservation, whose symbols do
# not depend on the hooks, so this library links no telemetry flavour. In the isolated telemetry
# smoke configuration it is built with the sender hook, as the rest of the application is. The
# moc object of the library references both windows, so a target that uses TrainFilterButton,
# DiagramWindow or TimetableTableWindow also links one telemetry library. So does a target that
# uses AreaCapacityWindow or DelayAnalysisWindow, which call it as well.
add_library(egtrain_diagrams STATIC
    ${SRC_DIR}/diagrams/AreaCapacity.cpp
    ${SRC_DIR}/diagrams/AreaCapacityWindow.cpp
    ${SRC_DIR}/diagrams/BlockingTimeData.cpp
    ${SRC_DIR}/diagrams/BlockingTimeDiagram.cpp
    ${SRC_DIR}/diagrams/BlockingTimeScope.cpp
    ${SRC_DIR}/diagrams/BlockSectionOverlay.cpp
    ${SRC_DIR}/diagrams/CapacityAnalysis.cpp
    ${SRC_DIR}/diagrams/CapacityScope.cpp
    ${SRC_DIR}/diagrams/CapacityWindow.cpp
    ${SRC_DIR}/diagrams/ChartTicks.cpp
    ${SRC_DIR}/diagrams/CompletedRunCapture.cpp
    ${SRC_DIR}/diagrams/DelayAnalysis.cpp
    ${SRC_DIR}/diagrams/DelayAnalysisWindow.cpp
    ${SRC_DIR}/diagrams/DiagramKey.cpp
    ${SRC_DIR}/diagrams/DiagramWindow.cpp
    ${SRC_DIR}/diagrams/PositionScale.cpp
    ${SRC_DIR}/diagrams/ResultFrame.cpp
    ${SRC_DIR}/diagrams/RouteCharts.cpp
    ${SRC_DIR}/diagrams/RouteDiagramCoordinates.cpp
    ${SRC_DIR}/diagrams/RouteDiagramData.cpp
    ${SRC_DIR}/diagrams/RouteReferenceChoice.cpp
    ${SRC_DIR}/diagrams/RunCsv.cpp
    ${SRC_DIR}/diagrams/RunResults.cpp
    ${SRC_DIR}/diagrams/SeriesMeta.cpp
    ${SRC_DIR}/diagrams/TimetableTableWindow.cpp
    ${SRC_DIR}/diagrams/TractionCurve.cpp
    ${SRC_DIR}/diagrams/TrainColors.cpp
    ${SRC_DIR}/diagrams/TrainFilterButton.cpp
    ${SRC_DIR}/diagrams/TrainKeyList.cpp)
target_link_libraries(egtrain_diagrams
    PUBLIC Qt::Core Qt::Gui Qt::Network Qt::Widgets Qt::Charts egtrain_sim)
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
#   egtrain_miniz                   vendored code
#   egtrain_sim                     legacy code with 104 warning lines, see #453, #454 and #482
#   QEGTRAIN                        98 warning lines, 41 of them in app/MainWindow.cpp and the rest
#                                   from the simulation headers, until the window is split up;
#                                   app/main.cpp, app/GuardedApplication.cpp, app/UpdateTelemetryController.cpp,
#                                   update/SelfUpdater.cpp, update/UpdateNotices.cpp, telemetry/TelemetryConsentDialog.cpp
#                                   and widgets/SettingsDialog.cpp build here too; test_selfupdater builds SelfUpdater.cpp
#                                   with the flags, test_updatetelemetrycontroller builds UpdateTelemetryController.cpp
#                                   with them, test_guardedapplication builds GuardedApplication.cpp with them and
#                                   test_settingsdialog builds SettingsDialog.cpp with them
# The test executables are strict. EGTRAIN/QEGTRAIN/tests/CMakeLists.txt makes the headers below SRC_DIR system headers
# for the tests that include the simulation headers, so the warnings of those headers do not count there.

# --- Sources ---
set(EGTRAIN_SOURCES
    ${SRC_DIR}/app/CrashNote.cpp
    ${SRC_DIR}/app/main.cpp
    ${SRC_DIR}/app/GuardedApplication.cpp
    ${SRC_DIR}/app/MainWindow.cpp
    ${SRC_DIR}/app/EditorDocks.cpp
    ${SRC_DIR}/app/ResultViews.cpp
    ${SRC_DIR}/app/RunController.cpp
    ${SRC_DIR}/app/RunResultsDock.cpp
    ${SRC_DIR}/app/RuntimeGeometryFromRun.cpp
    ${SRC_DIR}/app/SceneOutputDirectory.cpp
    ${SRC_DIR}/app/SceneSession.cpp
    ${SRC_DIR}/app/StartupTiming.cpp
    ${SRC_DIR}/app/UpdateTelemetryController.cpp
    ${SRC_DIR}/telemetry/TelemetryConsentDialog.cpp
    ${SRC_DIR}/update/SelfUpdater.cpp
    ${SRC_DIR}/update/UpdateNotices.cpp
    ${SRC_DIR}/widgets/SettingsDialog.cpp
)

set(EGTRAIN_HEADERS
    ${SRC_DIR}/widgets/DialogLayout.h
    ${SRC_DIR}/widgets/ChoiceComboBox.h
    ${SRC_DIR}/widgets/EditorLayout.h
    ${SRC_DIR}/widgets/UserPrompts.h
    ${SRC_DIR}/simulation/Infrastructure.h
    ${SRC_DIR}/simulation/InitialParameters.h
    ${SRC_DIR}/app/CrashNote.h
    ${SRC_DIR}/app/GuardedApplication.h
    ${SRC_DIR}/app/MainWindow.h
    ${SRC_DIR}/app/MainWindowTestHooks.h
    ${SRC_DIR}/app/ResultViews.h
    ${SRC_DIR}/app/RunController.h
    ${SRC_DIR}/app/RunResultsDock.h
    ${SRC_DIR}/app/RuntimeGeometryFromRun.h
    ${SRC_DIR}/app/SceneOutputDirectory.h
    ${SRC_DIR}/app/SceneSession.h
    ${SRC_DIR}/app/StartupTiming.h
    ${SRC_DIR}/app/UpdateTelemetryController.h
    ${SRC_DIR}/graphics/RuntimeGeometrySource.h
    ${SRC_DIR}/graphics/SignalItemRoles.h
    ${SRC_DIR}/simulation/NumberGenerator.h
    ${SRC_DIR}/util/Log.h
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
    egtrain_recovery
    egtrain_sim
    egtrain_runselection
    egtrain_scene
    egtrain_util
    nlohmann_json::nlohmann_json
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Charts
    Qt::Svg
    Qt::Network
)

message(STATUS "ZeroMQ + nlohmann_json found - live stream enabled")

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
    set_target_properties(QEGTRAIN egtrain_util egtrain_scene egtrain_sim
        egtrain_graphics egtrain_widgets egtrain_http egtrain_update egtrain_sceneopen egtrain_openrelay egtrain_recovery egtrain_dispatch egtrain_diagrams
        ${EGTRAIN_TELEMETRY_LIBS}
        PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "LTO: enabled for Release builds")
else()
    message(STATUS "LTO: not enabled (${EGTRAIN_IPO_ERROR})")
endif()

if(WIN32)
    set_target_properties(QEGTRAIN PROPERTIES WIN32_EXECUTABLE TRUE)
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
    include(${SRC_DIR}/tests/CMakeLists.txt)
endif()

# Install
install(TARGETS QEGTRAIN scene_tool
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
    FRAMEWORK DESTINATION .)
install(DIRECTORY ${SRC_DIR}/Scenes/ DESTINATION share/EGTRAIN/Scenes)

# Documentation site targets `docs` and `docs-preview`; see docs/development/testing.md#documentation-site.
# Without the tools they print how to install them.
find_package(Doxygen 1.18 QUIET COMPONENTS dot)
set(egtrain_docs_venv "${CMAKE_BINARY_DIR}/docs-venv")
if(WIN32)
	set(egtrain_docs_bin "${egtrain_docs_venv}/Scripts")
else()
	set(egtrain_docs_bin "${egtrain_docs_venv}/bin")
endif()
set(egtrain_docs_install "The docs targets need Doxygen 1.18 or newer, Graphviz and the packages of docs/requirements.txt. \
Install Doxygen and Graphviz, then run with Python 3.12 or newer: python3 -m venv ${egtrain_docs_venv} and \
${egtrain_docs_bin}/pip install -r ${CMAKE_SOURCE_DIR}/docs/requirements.txt. Then configure again.")
if(DOXYGEN_FOUND AND EXISTS "${egtrain_docs_bin}/sphinx-build${CMAKE_EXECUTABLE_SUFFIX}")
	get_filename_component(egtrain_dot_dir "${DOXYGEN_DOT_EXECUTABLE}" DIRECTORY)
	file(WRITE "${CMAKE_BINARY_DIR}/Doxyfile"
		"@INCLUDE = \"${CMAKE_SOURCE_DIR}/docs/Doxyfile\"\nOUTPUT_DIRECTORY = \"${CMAKE_BINARY_DIR}/docs\"\nDOT_PATH = \"${egtrain_dot_dir}\"\n")
	add_custom_target(docs
		COMMAND Doxygen::doxygen "${CMAKE_BINARY_DIR}/Doxyfile"
		COMMAND "${egtrain_docs_bin}/sphinx-build" -W -b html "${CMAKE_SOURCE_DIR}/docs" "${CMAKE_BINARY_DIR}/docs"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		COMMENT "Building the documentation site in ${CMAKE_BINARY_DIR}/docs"
		VERBATIM)
	add_custom_target(docs-preview
		COMMAND "${egtrain_docs_bin}/sphinx-autobuild" --port 8000 "${CMAKE_SOURCE_DIR}/docs" "${CMAKE_BINARY_DIR}/docs"
		USES_TERMINAL
		VERBATIM)
else()
	foreach(egtrain_docs_target IN ITEMS docs docs-preview)
		add_custom_target(${egtrain_docs_target}
			COMMAND "${CMAKE_COMMAND}" -E echo "${egtrain_docs_install}"
			COMMAND "${CMAKE_COMMAND}" -E false
			VERBATIM)
	endforeach()
endif()

# The manual of the application: the pages of docs/guides built a second time with Sphinx's qthelp builder into the Qt help file
# EGTRAIN.qch, which goes next to the program, or into the resources of the macOS app. It needs Sphinx in the virtual environment of
# the docs targets. Without it the application opens the published guides in the web browser instead.
if(EXISTS "${egtrain_docs_bin}/sphinx-build${CMAKE_EXECUTABLE_SUFFIX}")
	set(egtrain_manual "${CMAKE_BINARY_DIR}/manual/EGTRAIN.qch")
	file(GLOB egtrain_manual_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/docs/guides/*" "${CMAKE_SOURCE_DIR}/docs/images/*")
	add_custom_command(OUTPUT "${egtrain_manual}"
		COMMAND "${egtrain_docs_bin}/sphinx-build" -q -b qthelp -t manual -D "version=${PROJECT_VERSION}"
			-D "html_title=EGTRAIN ${PROJECT_VERSION} manual" "${CMAKE_SOURCE_DIR}/docs" "${CMAKE_BINARY_DIR}/manual/qthelp"
		COMMAND Qt6::qhelpgenerator "${CMAKE_BINARY_DIR}/manual/qthelp/EGTRAIN.qhp" -o "${egtrain_manual}"
		DEPENDS ${egtrain_manual_sources} "${CMAKE_SOURCE_DIR}/docs/conf.py" "${CMAKE_SOURCE_DIR}/docs/index.md"
		COMMENT "Building the manual ${egtrain_manual}"
		VERBATIM)
	# Each build copies the manual again when it changes.
	if(APPLE)
		target_sources(QEGTRAIN PRIVATE "${egtrain_manual}")
		set_source_files_properties("${egtrain_manual}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
	else()
		add_custom_target(manual ALL
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${egtrain_manual}" "$<TARGET_FILE_DIR:QEGTRAIN>/EGTRAIN.qch"
			DEPENDS "${egtrain_manual}"
			VERBATIM)
	endif()
	message(STATUS "Manual: built from docs/guides into ${egtrain_manual}")
else()
	message(STATUS "Manual: not built, Sphinx is not in ${egtrain_docs_venv}; the application opens the published guides")
endif()
