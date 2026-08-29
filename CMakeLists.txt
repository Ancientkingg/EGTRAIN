cmake_minimum_required(VERSION 3.16)
project(EGTRAIN VERSION 1.0.0 LANGUAGES C CXX)

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
endif()

option(EGTRAIN_ENABLE_SANITIZERS "Enable AddressSanitizer/UndefinedBehaviorSanitizer for local debug checks" OFF)
option(EGTRAIN_PACKAGED_BUILD "Enable self-update support for a release package" OFF)

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
set_target_properties(egtrain_miniz PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

# Path-qualified includes (e.g. graphics/items/NodeItem.h) resolve against SRC_DIR
include_directories(${SRC_DIR})

# --- Dependencies ---
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Charts Svg Network)

# OpenMP is required because project headers include omp.h.
if(APPLE)
    # AppleClang doesn't support -fopenmp directly; use -Xpreprocessor.
    # Locate Homebrew's libomp on Apple Silicon and Intel Macs.
    find_path(EGTRAIN_OPENMP_INCLUDE omp.h
        HINTS /opt/homebrew/opt/libomp/include /usr/local/opt/libomp/include)
    find_library(EGTRAIN_OPENMP_LINK_LIBRARIES omp
        HINTS /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib)
    if(EGTRAIN_OPENMP_INCLUDE AND EGTRAIN_OPENMP_LINK_LIBRARIES)
        set(EGTRAIN_OPENMP_COMPILE_OPTIONS "-Xpreprocessor;-fopenmp")
        message(STATUS "OpenMP: using Homebrew libomp (${EGTRAIN_OPENMP_LINK_LIBRARIES})")
    else()
        message(FATAL_ERROR "OpenMP libomp not found. Install libomp.")
    endif()
else()
    find_package(OpenMP REQUIRED)
    set(EGTRAIN_OPENMP_COMPILE_OPTIONS ${OpenMP_CXX_FLAGS})
endif()

# --- ZeroMQ / nlohmann_json ---
find_package(cppzmq REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Sources (mirrors QEGTRAIN.vcxproj ClCompile items) ---
set(EGTRAIN_SOURCES
    ${SRC_DIR}/graphics/items/ConnectionItem.cpp
    ${SRC_DIR}/widgets/ConsoleWidget.cpp
    ${SRC_DIR}/app/DispatchController.cpp
    ${SRC_DIR}/widgets/InfoDockWidget.cpp
    ${SRC_DIR}/simulation/Infrastructure.cpp
    ${SRC_DIR}/simulation/InitialParameters.cpp
    ${SRC_DIR}/io/RailMLParser.cpp
    ${SRC_DIR}/app/main.cpp
    ${SRC_DIR}/app/MainWindow.cpp
    ${SRC_DIR}/graphics/items/HighlightEffect.cpp
    ${SRC_DIR}/graphics/items/NodeItem.cpp
    ${SRC_DIR}/graphics/items/TrackLineItem.cpp
    ${SRC_DIR}/graphics/items/TrainBadgeItem.cpp
    ${SRC_DIR}/widgets/NetworkLegendWidget.cpp
    ${SRC_DIR}/graphics/items/IconItem.cpp
    ${SRC_DIR}/graphics/items/StationNodeItem.cpp
    ${SRC_DIR}/graphics/items/StationOverlayItem.cpp
    ${SRC_DIR}/graphics/NetworkScene.cpp
    ${SRC_DIR}/graphics/NetworkView.cpp
    ${SRC_DIR}/simulation/NumberGenerator.cpp
    ${SRC_DIR}/simulation/Optimisation.cpp
    ${SRC_DIR}/util/Logger.cpp
    ${SRC_DIR}/util/PlaybackProfiler.cpp
    ${SRC_DIR}/graphics/items/PassengerItem.cpp
    ${SRC_DIR}/simulation/Passengers.cpp
    ${SRC_DIR}/graphics/items/PlatformItem.cpp
    ${SRC_DIR}/io/third_party/pugixml.cpp
    ${SRC_DIR}/simulation/Rescheduling.cpp
    ${SRC_DIR}/simulation/RollingStock.cpp
    ${SRC_DIR}/simulation/Signalling.cpp
    ${SRC_DIR}/graphics/items/SignalItem.cpp
    ${SRC_DIR}/simulation/Simulation.cpp
    ${SRC_DIR}/simulation/SimulationWorker.cpp
    ${SRC_DIR}/util/TimeFormat.cpp
    ${SRC_DIR}/util/Version.cpp
	${SRC_DIR}/update/ReleaseInfo.cpp
	${SRC_DIR}/update/UpdateChecker.cpp
	${SRC_DIR}/update/UpdateSettings.cpp
	${SRC_DIR}/update/UpdatePreparation.cpp
	${SRC_DIR}/update/SelfUpdater.cpp
	${SRC_DIR}/scene/SceneDiagnostic.cpp
	${SRC_DIR}/scene/SceneModel.cpp
	${SRC_DIR}/scene/SectionInventory.cpp
	${SRC_DIR}/scene/SceneBundle.cpp
	${SRC_DIR}/scene/SceneCompatibility.cpp
	${SRC_DIR}/scene/SceneMigration.cpp
	${SRC_DIR}/scene/SceneValidator.cpp
	${SRC_DIR}/scene/SceneImporter.cpp
	${SRC_DIR}/scene/SceneWriter.cpp
	${SRC_DIR}/scene/TrackPreview.cpp
	${SRC_DIR}/widgets/TimeProgressBar.cpp
    ${SRC_DIR}/graphics/items/TrainItemGroup.cpp
    ${SRC_DIR}/graphics/items/TrainBodyItem.cpp
    ${SRC_DIR}/graphics/items/VirtualArcItem.cpp
    ${SRC_DIR}/util/SpeedFormat.cpp
    ${SRC_DIR}/util/CsvWriter.cpp
    ${SRC_DIR}/diagrams/DiagramWindow.cpp
    ${SRC_DIR}/diagrams/TrainFilterButton.cpp
    ${SRC_DIR}/diagrams/TimetableTableWindow.cpp
    ${SRC_DIR}/diagrams/RunResults.cpp
    ${SRC_DIR}/util/TrajectoryUtil.cpp
    ${SRC_DIR}/diagrams/BlockingTimeDiagram.cpp
    ${SRC_DIR}/diagrams/CapacityAnalysis.cpp
    ${SRC_DIR}/diagrams/TractionCurve.cpp
    ${SRC_DIR}/graphics/VisualPolish.cpp
    ${SRC_DIR}/scene/SceneExporter.cpp
)

set(EGTRAIN_HEADERS
    ${SRC_DIR}/graphics/items/ConnectionItem.h
    ${SRC_DIR}/widgets/ConsoleWidget.h
    ${SRC_DIR}/app/DispatchController.h
    ${SRC_DIR}/widgets/InfoDockWidget.h
    ${SRC_DIR}/simulation/Infrastructure.h
    ${SRC_DIR}/simulation/InitialParameters.h
    ${SRC_DIR}/io/RailMLParser.h
    ${SRC_DIR}/app/MainWindow.h
    ${SRC_DIR}/graphics/items/HighlightEffect.h
    ${SRC_DIR}/graphics/items/NodeItem.h
    ${SRC_DIR}/graphics/items/TrackLineItem.h
    ${SRC_DIR}/graphics/items/TrainBadgeItem.h
    ${SRC_DIR}/widgets/NetworkLegendWidget.h
    ${SRC_DIR}/graphics/items/IconItem.h
    ${SRC_DIR}/graphics/items/StationNodeItem.h
    ${SRC_DIR}/graphics/items/StationOverlayItem.h
    ${SRC_DIR}/graphics/NetworkScene.h
    ${SRC_DIR}/graphics/NetworkView.h
    ${SRC_DIR}/simulation/NumberGenerator.h
    ${SRC_DIR}/simulation/Optimisation.h
    ${SRC_DIR}/util/Logger.hpp
    ${SRC_DIR}/util/PlaybackProfiler.h
    ${SRC_DIR}/graphics/items/PassengerItem.h
    ${SRC_DIR}/simulation/Passengers.h
    ${SRC_DIR}/graphics/items/PlatformItem.h
    ${SRC_DIR}/util/portability.h
    ${SRC_DIR}/simulation/Rescheduling.h
    ${SRC_DIR}/simulation/RollingStock.h
    ${SRC_DIR}/simulation/Signalling.h
    ${SRC_DIR}/graphics/items/SignalItem.h
    ${SRC_DIR}/simulation/Simulation.h
    ${SRC_DIR}/simulation/SimulationWorker.h
    ${SRC_DIR}/util/TimeFormat.h
    ${SRC_DIR}/util/Version.h
	${SRC_DIR}/update/ReleaseInfo.h
	${SRC_DIR}/update/UpdateChecker.h
	${SRC_DIR}/update/UpdateSettings.h
	${SRC_DIR}/update/UpdatePreparation.h
	${SRC_DIR}/update/SelfUpdater.h
    ${SRC_DIR}/scene/SceneDiagnostic.h
	${SRC_DIR}/scene/SceneModel.h
	${SRC_DIR}/scene/SectionInventory.h
	${SRC_DIR}/scene/SceneBundle.h
	${SRC_DIR}/scene/SceneCompatibility.h
	${SRC_DIR}/scene/SceneMigration.h
	${SRC_DIR}/scene/SceneValidator.h
	${SRC_DIR}/scene/SceneImporter.h
	${SRC_DIR}/scene/SceneWriter.h
	${SRC_DIR}/scene/TrackPreview.h
	${SRC_DIR}/widgets/TimeProgressBar.h
    ${SRC_DIR}/util/timeutil.hpp
    ${SRC_DIR}/graphics/items/TrainItemGroup.h
    ${SRC_DIR}/graphics/items/TrainBodyItem.h
    ${SRC_DIR}/util/Util.hpp
    ${SRC_DIR}/graphics/items/VirtualArcItem.h
    ${SRC_DIR}/util/SpeedFormat.h
    ${SRC_DIR}/util/CsvWriter.h
    ${SRC_DIR}/diagrams/DiagramWindow.h
    ${SRC_DIR}/diagrams/TrainFilterButton.h
    ${SRC_DIR}/diagrams/TimetableTableWindow.h
    ${SRC_DIR}/diagrams/RunResults.h
    ${SRC_DIR}/util/TrajectoryUtil.h
    ${SRC_DIR}/diagrams/BlockingTimeDiagram.h
    ${SRC_DIR}/diagrams/CapacityAnalysis.h
    ${SRC_DIR}/diagrams/TractionCurve.h
    ${SRC_DIR}/graphics/VisualPolish.h
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
if(EGTRAIN_OPENMP_INCLUDE)
    target_include_directories(QEGTRAIN PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
endif()

target_link_libraries(QEGTRAIN PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Charts
    Qt5::Svg
    Qt5::Network
)

if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
    target_compile_options(QEGTRAIN PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
endif()
if(EGTRAIN_OPENMP_LINK_LIBRARIES)
    target_link_libraries(QEGTRAIN PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
elseif(OpenMP_CXX_FOUND)
    target_link_libraries(QEGTRAIN PRIVATE OpenMP::OpenMP_CXX)
endif()
if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
    message(STATUS "OpenMP: enabled")
elseif(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found via CMake: ${OpenMP_CXX_FLAGS}")
else()
    message(STATUS "OpenMP not found - parallel regions will compile as single-threaded")
endif()

target_link_libraries(QEGTRAIN PRIVATE cppzmq nlohmann_json::nlohmann_json)
target_link_libraries(QEGTRAIN PRIVATE egtrain_miniz)
message(STATUS "ZeroMQ + nlohmann_json found - traffic-state sharing enabled")

add_executable(egtrain_update_helper
	${SRC_DIR}/update/UpdateHelper.cpp)
if(MSVC)
    set_property(TARGET egtrain_update_helper PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(EGTRAIN_PACKAGED_BUILD)
    enable_testing()
    add_executable(test_updatehelper
        ${SRC_DIR}/tests/test_updatehelper.cpp)
    target_link_libraries(test_updatehelper PRIVATE Qt5::Core)
    add_test(NAME test_updatehelper COMMAND test_updatehelper $<TARGET_FILE:egtrain_update_helper>)
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
    set_target_properties(QEGTRAIN PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
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

add_executable(scene_tool
	${SRC_DIR}/scene/SceneTool.cpp
	${SRC_DIR}/scene/SceneDiagnostic.cpp
	${SRC_DIR}/scene/SceneModel.cpp
	${SRC_DIR}/scene/SectionInventory.cpp
	${SRC_DIR}/scene/SceneBundle.cpp
	${SRC_DIR}/scene/SceneValidator.cpp
	${SRC_DIR}/scene/SceneImporter.cpp
	${SRC_DIR}/scene/SceneExporter.cpp
	${SRC_DIR}/scene/SceneWriter.cpp)
target_link_libraries(scene_tool PRIVATE nlohmann_json::nlohmann_json egtrain_miniz)

# --- Unit tests (opt-in) ---
option(EGTRAIN_BUILD_TESTS "Build unit tests" OFF)
if(EGTRAIN_BUILD_TESTS)
    enable_testing()
    add_executable(test_timeformat
        ${SRC_DIR}/tests/test_timeformat.cpp
        ${SRC_DIR}/util/TimeFormat.cpp)
    add_test(NAME test_timeformat COMMAND test_timeformat)

    add_executable(test_version
        ${SRC_DIR}/tests/test_version.cpp
        ${SRC_DIR}/util/Version.cpp)
    add_test(NAME test_version COMMAND test_version)

    add_executable(test_updatechecker
        ${SRC_DIR}/tests/test_updatechecker.cpp
        ${SRC_DIR}/update/ReleaseInfo.cpp
        ${SRC_DIR}/update/UpdateSettings.cpp
        ${SRC_DIR}/util/Version.cpp)
    target_link_libraries(test_updatechecker PRIVATE Qt5::Core Qt5::Network)
    add_test(NAME test_updatechecker COMMAND test_updatechecker)

    add_executable(test_updatestaging
        ${SRC_DIR}/tests/test_updatestaging.cpp)
    target_link_libraries(test_updatestaging PRIVATE Qt5::Core)
    add_test(NAME test_updatestaging COMMAND test_updatestaging)

    add_executable(test_updatepreparation
        ${SRC_DIR}/tests/test_updatepreparation.cpp
        ${SRC_DIR}/update/UpdatePreparation.cpp)
    target_link_libraries(test_updatepreparation PRIVATE Qt5::Core)
    add_test(NAME test_updatepreparation COMMAND test_updatepreparation)

    if(NOT TARGET test_updatehelper)
        add_executable(test_updatehelper
            ${SRC_DIR}/tests/test_updatehelper.cpp)
        target_link_libraries(test_updatehelper PRIVATE Qt5::Core)
        add_test(NAME test_updatehelper COMMAND test_updatehelper $<TARGET_FILE:egtrain_update_helper>)
    endif()

    add_executable(test_guisimulationsnapshot
        ${SRC_DIR}/tests/test_guisimulationsnapshot.cpp)
    target_include_directories(test_guisimulationsnapshot PRIVATE ${SRC_DIR})
    add_test(NAME test_guisimulationsnapshot COMMAND test_guisimulationsnapshot)

	add_executable(test_trackpreview
		${SRC_DIR}/tests/test_trackpreview.cpp
		${SRC_DIR}/scene/TrackPreview.cpp
		${SRC_DIR}/scene/SectionInventory.cpp)
	target_link_libraries(test_trackpreview PRIVATE nlohmann_json::nlohmann_json)
	add_test(NAME test_trackpreview COMMAND test_trackpreview)

    add_executable(test_speedformat
        ${SRC_DIR}/tests/test_speedformat.cpp
        ${SRC_DIR}/util/SpeedFormat.cpp)
    add_test(NAME test_speedformat COMMAND test_speedformat)

    add_executable(test_trajectory
        ${SRC_DIR}/tests/test_trajectory.cpp
        ${SRC_DIR}/util/TrajectoryUtil.cpp)
    add_test(NAME test_trajectory COMMAND test_trajectory)

    add_executable(test_runresults
        ${SRC_DIR}/tests/test_runresults.cpp
        ${SRC_DIR}/diagrams/RunResults.cpp
        ${SRC_DIR}/simulation/Infrastructure.cpp
        ${SRC_DIR}/simulation/InitialParameters.cpp
        ${SRC_DIR}/simulation/NumberGenerator.cpp
        ${SRC_DIR}/simulation/Optimisation.cpp
        ${SRC_DIR}/simulation/Passengers.cpp
        ${SRC_DIR}/simulation/RollingStock.cpp
        ${SRC_DIR}/simulation/Signalling.cpp
        ${SRC_DIR}/simulation/Simulation.cpp
        ${SRC_DIR}/util/Logger.cpp
        ${SRC_DIR}/util/TrajectoryUtil.cpp
        ${SRC_DIR}/scene/SceneDiagnostic.cpp
        ${SRC_DIR}/scene/SceneModel.cpp
        ${SRC_DIR}/scene/SectionInventory.cpp)
    target_include_directories(test_runresults PRIVATE ${SRC_DIR})
    target_link_libraries(test_runresults PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts cppzmq nlohmann_json::nlohmann_json)
    if(EGTRAIN_OPENMP_INCLUDE)
        target_include_directories(test_runresults PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
    endif()
    if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
        target_compile_options(test_runresults PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
    endif()
    if(EGTRAIN_OPENMP_LINK_LIBRARIES)
        target_link_libraries(test_runresults PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
    elseif(OpenMP_CXX_FOUND)
        target_link_libraries(test_runresults PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME test_runresults COMMAND test_runresults)

    add_executable(test_blockingtimediagram
        ${SRC_DIR}/tests/test_blockingtimediagram.cpp
        ${SRC_DIR}/diagrams/BlockingTimeDiagram.cpp)
    add_test(NAME test_blockingtimediagram COMMAND test_blockingtimediagram)

    add_executable(test_capacityanalysis
        ${SRC_DIR}/tests/test_capacityanalysis.cpp
        ${SRC_DIR}/diagrams/CapacityAnalysis.cpp
        ${SRC_DIR}/diagrams/BlockingTimeDiagram.cpp)
    add_test(NAME test_capacityanalysis COMMAND test_capacityanalysis)

    add_executable(test_diagramwindow
        ${SRC_DIR}/tests/test_diagramwindow.cpp
        ${SRC_DIR}/diagrams/DiagramWindow.cpp
        ${SRC_DIR}/diagrams/TrainFilterButton.cpp
        ${SRC_DIR}/util/TimeFormat.cpp)
    target_include_directories(test_diagramwindow PRIVATE ${SRC_DIR})
    target_link_libraries(test_diagramwindow PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)
    add_test(NAME test_diagramwindow COMMAND test_diagramwindow)
    set_tests_properties(test_diagramwindow PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_visualpolish
        ${SRC_DIR}/tests/test_visualpolish.cpp
		${SRC_DIR}/graphics/items/TrainBadgeItem.cpp
		${SRC_DIR}/graphics/items/SignalItem.cpp
        ${SRC_DIR}/graphics/VisualPolish.cpp)
    target_link_libraries(test_visualpolish PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_visualpolish COMMAND test_visualpolish)

    add_executable(test_networklegend
        ${SRC_DIR}/tests/test_networklegend.cpp
        ${SRC_DIR}/widgets/NetworkLegendWidget.cpp
        ${SRC_DIR}/graphics/VisualPolish.cpp)
    target_link_libraries(test_networklegend PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_networklegend COMMAND test_networklegend)
    set_tests_properties(test_networklegend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_stationoverlay
        ${SRC_DIR}/tests/test_stationoverlay.cpp
        ${SRC_DIR}/graphics/items/StationOverlayItem.cpp
        ${SRC_DIR}/graphics/VisualPolish.cpp
        ${SRC_DIR}/graphics/NetworkScene.cpp
        ${SRC_DIR}/graphics/items/NodeItem.cpp
        ${SRC_DIR}/graphics/items/StationNodeItem.cpp
        ${SRC_DIR}/graphics/items/TrackLineItem.cpp
        ${SRC_DIR}/graphics/items/ConnectionItem.cpp
        ${SRC_DIR}/graphics/items/SignalItem.cpp
        ${SRC_DIR}/graphics/items/TrainBodyItem.cpp
        ${SRC_DIR}/graphics/items/PassengerItem.cpp)
    target_include_directories(test_stationoverlay PRIVATE ${SRC_DIR})
    target_link_libraries(test_stationoverlay PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    if(EGTRAIN_OPENMP_INCLUDE)
        target_include_directories(test_stationoverlay PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
    endif()
    if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
        target_compile_options(test_stationoverlay PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
    endif()
    if(EGTRAIN_OPENMP_LINK_LIBRARIES)
        target_link_libraries(test_stationoverlay PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
    elseif(OpenMP_CXX_FOUND)
        target_link_libraries(test_stationoverlay PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME test_stationoverlay COMMAND test_stationoverlay)
    set_tests_properties(test_stationoverlay PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_networkscene_selection
        ${SRC_DIR}/tests/test_networkscene_selection.cpp
        ${SRC_DIR}/graphics/NetworkScene.cpp
        ${SRC_DIR}/graphics/items/NodeItem.cpp
        ${SRC_DIR}/graphics/items/StationNodeItem.cpp
        ${SRC_DIR}/graphics/items/TrackLineItem.cpp
        ${SRC_DIR}/graphics/items/ConnectionItem.cpp
        ${SRC_DIR}/graphics/items/SignalItem.cpp
        ${SRC_DIR}/graphics/items/TrainBodyItem.cpp
        ${SRC_DIR}/graphics/items/PassengerItem.cpp
        ${SRC_DIR}/graphics/VisualPolish.cpp)
    if(EGTRAIN_OPENMP_INCLUDE)
        target_include_directories(test_networkscene_selection PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
    endif()
    target_link_libraries(test_networkscene_selection PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    add_test(NAME test_networkscene_selection COMMAND test_networkscene_selection)
    set_tests_properties(test_networkscene_selection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_playbackprofiler
        ${SRC_DIR}/tests/test_playbackprofiler.cpp
        ${SRC_DIR}/util/PlaybackProfiler.cpp)
    target_include_directories(test_playbackprofiler PRIVATE ${SRC_DIR})
    target_link_libraries(test_playbackprofiler PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME test_playbackprofiler COMMAND test_playbackprofiler)

    add_executable(test_networkview
        ${SRC_DIR}/tests/test_networkview.cpp
        ${SRC_DIR}/graphics/NetworkView.cpp
        ${SRC_DIR}/util/PlaybackProfiler.cpp
        ${SRC_DIR}/graphics/items/TrackLineItem.cpp
        ${SRC_DIR}/graphics/items/VirtualArcItem.cpp
        ${SRC_DIR}/graphics/items/ConnectionItem.cpp
        ${SRC_DIR}/graphics/VisualPolish.cpp)
    target_include_directories(test_networkview PRIVATE ${SRC_DIR})
    if(EGTRAIN_OPENMP_INCLUDE)
        target_include_directories(test_networkview PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
    endif()
    target_link_libraries(test_networkview PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets cppzmq nlohmann_json::nlohmann_json)
    if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
        target_compile_options(test_networkview PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
    endif()
    if(EGTRAIN_OPENMP_LINK_LIBRARIES)
        target_link_libraries(test_networkview PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
    elseif(OpenMP_CXX_FOUND)
        target_link_libraries(test_networkview PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME test_networkview COMMAND test_networkview)
    set_tests_properties(test_networkview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_timeprogressbar
        ${SRC_DIR}/tests/test_timeprogressbar.cpp
        ${SRC_DIR}/widgets/TimeProgressBar.cpp
        ${SRC_DIR}/util/TimeFormat.cpp)
    target_include_directories(test_timeprogressbar PRIVATE ${SRC_DIR})
    target_link_libraries(test_timeprogressbar PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_timeprogressbar COMMAND test_timeprogressbar)
    set_tests_properties(test_timeprogressbar PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

	    add_executable(test_highlighteffect
	        ${SRC_DIR}/tests/test_highlighteffect.cpp
	        ${SRC_DIR}/graphics/items/HighlightEffect.cpp)
	    target_link_libraries(test_highlighteffect PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
	    add_test(NAME test_highlighteffect COMMAND test_highlighteffect)
	    set_tests_properties(test_highlighteffect PROPERTIES
	        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_csvwriter
        ${SRC_DIR}/tests/test_csvwriter.cpp
        ${SRC_DIR}/util/CsvWriter.cpp)
    target_include_directories(test_csvwriter PRIVATE ${SRC_DIR})
    add_test(NAME test_csvwriter COMMAND test_csvwriter)

    add_executable(test_tractioncurve
        ${SRC_DIR}/tests/test_tractioncurve.cpp
        ${SRC_DIR}/diagrams/TractionCurve.cpp)
    target_include_directories(test_tractioncurve PRIVATE ${SRC_DIR})
    add_test(NAME test_tractioncurve COMMAND test_tractioncurve)

    add_executable(test_consolestream
        ${SRC_DIR}/tests/test_consolestream.cpp
        ${SRC_DIR}/widgets/ConsoleWidget.cpp)
    target_link_libraries(test_consolestream PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME test_consolestream COMMAND test_consolestream)

    add_test(NAME test_qt_resources
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/e2e/test_qt_resources.py)
    add_test(NAME test_app_icon_config
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/e2e/test_app_icon_config.py)
    add_executable(test_qt_resource_images
        ${SRC_DIR}/tests/test_qt_resource_images.cpp
        ${EGTRAIN_RESOURCES})
    target_link_libraries(test_qt_resource_images PRIVATE Qt5::Core Qt5::Gui Qt5::Svg)
    add_test(NAME test_qt_resource_images COMMAND test_qt_resource_images)
    set_tests_properties(test_qt_resource_images PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    add_executable(test_scenevalidator
        ${SRC_DIR}/tests/test_scenevalidator.cpp
        ${SRC_DIR}/scene/SceneDiagnostic.cpp
        ${SRC_DIR}/scene/SceneModel.cpp
        ${SRC_DIR}/scene/SectionInventory.cpp
        ${SRC_DIR}/scene/SceneValidator.cpp)
    target_link_libraries(test_scenevalidator PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME test_scenevalidator
        COMMAND test_scenevalidator ${SRC_DIR}/tests/fixtures/scenes/minimal)

    add_executable(test_sceneimporter
        ${SRC_DIR}/tests/test_sceneimporter.cpp
        ${SRC_DIR}/scene/SceneDiagnostic.cpp
        ${SRC_DIR}/scene/SceneModel.cpp
        ${SRC_DIR}/scene/SectionInventory.cpp
        ${SRC_DIR}/scene/SceneValidator.cpp
        ${SRC_DIR}/scene/SceneImporter.cpp)
    target_link_libraries(test_sceneimporter PRIVATE nlohmann_json::nlohmann_json)
	add_test(NAME test_sceneimporter COMMAND test_sceneimporter)

	add_executable(test_scenewriter
		${SRC_DIR}/tests/test_scenewriter.cpp
		${SRC_DIR}/scene/SceneDiagnostic.cpp
		${SRC_DIR}/scene/SceneModel.cpp
		${SRC_DIR}/scene/SceneWriter.cpp)
	target_link_libraries(test_scenewriter PRIVATE nlohmann_json::nlohmann_json)
	add_test(NAME test_scenewriter
		COMMAND test_scenewriter ${SRC_DIR}/Scenes)

	add_executable(test_sceneexporter
        ${SRC_DIR}/tests/test_sceneexporter.cpp
        ${SRC_DIR}/scene/SceneDiagnostic.cpp
        ${SRC_DIR}/scene/SceneModel.cpp
        ${SRC_DIR}/scene/SectionInventory.cpp
        ${SRC_DIR}/scene/SceneBundle.cpp
        ${SRC_DIR}/scene/SceneWriter.cpp
        ${SRC_DIR}/scene/SceneValidator.cpp
        ${SRC_DIR}/scene/SceneExporter.cpp)
    target_link_libraries(test_sceneexporter PRIVATE nlohmann_json::nlohmann_json egtrain_miniz)
	add_test(NAME test_sceneexporter COMMAND test_sceneexporter)

	add_executable(test_scenebundle
		${SRC_DIR}/tests/test_scenebundle.cpp
		${SRC_DIR}/scene/SceneDiagnostic.cpp
		${SRC_DIR}/scene/SceneModel.cpp
		${SRC_DIR}/scene/SceneBundle.cpp
		${SRC_DIR}/scene/SceneWriter.cpp)
	target_link_libraries(test_scenebundle PRIVATE nlohmann_json::nlohmann_json egtrain_miniz)
	add_test(NAME test_scenebundle
		COMMAND test_scenebundle ${SRC_DIR}/tests/fixtures/scenes/minimal)

	add_executable(test_scenecompatibility
		${SRC_DIR}/tests/test_scenecompatibility.cpp
		${SRC_DIR}/scene/SceneDiagnostic.cpp
		${SRC_DIR}/scene/SceneModel.cpp
		${SRC_DIR}/scene/SectionInventory.cpp
		${SRC_DIR}/scene/SceneBundle.cpp
		${SRC_DIR}/scene/SceneCompatibility.cpp
		${SRC_DIR}/scene/SceneMigration.cpp
		${SRC_DIR}/scene/SceneWriter.cpp)
	target_link_libraries(test_scenecompatibility PRIVATE nlohmann_json::nlohmann_json egtrain_miniz)
	add_test(NAME test_scenecompatibility
		COMMAND test_scenecompatibility ${SRC_DIR}/tests/fixtures/scenes/minimal)

	add_executable(test_scenebuilder
		${SRC_DIR}/tests/test_scenebuilder.cpp
		${SRC_DIR}/simulation/Infrastructure.cpp
		${SRC_DIR}/simulation/InitialParameters.cpp
		${SRC_DIR}/simulation/NumberGenerator.cpp
		${SRC_DIR}/util/Logger.cpp
		${SRC_DIR}/simulation/Signalling.cpp
		${SRC_DIR}/scene/SceneDiagnostic.cpp
		${SRC_DIR}/scene/SceneModel.cpp
		${SRC_DIR}/scene/SectionInventory.cpp)
	target_include_directories(test_scenebuilder PRIVATE ${SRC_DIR})
	target_link_libraries(test_scenebuilder PRIVATE nlohmann_json::nlohmann_json)
	if(EGTRAIN_OPENMP_INCLUDE)
		target_include_directories(test_scenebuilder PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
	endif()
	if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
		target_compile_options(test_scenebuilder PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
	endif()
	if(EGTRAIN_OPENMP_LINK_LIBRARIES)
		target_link_libraries(test_scenebuilder PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
	elseif(OpenMP_CXX_FOUND)
		target_link_libraries(test_scenebuilder PRIVATE OpenMP::OpenMP_CXX)
	endif()
	add_test(NAME test_scenebuilder COMMAND test_scenebuilder)

	add_executable(test_operationsbuilder
		${SRC_DIR}/tests/test_operationsbuilder.cpp
		${SRC_DIR}/simulation/Infrastructure.cpp
		${SRC_DIR}/simulation/InitialParameters.cpp
		${SRC_DIR}/simulation/NumberGenerator.cpp
		${SRC_DIR}/simulation/Optimisation.cpp
		${SRC_DIR}/simulation/Passengers.cpp
		${SRC_DIR}/simulation/RollingStock.cpp
		${SRC_DIR}/simulation/Signalling.cpp
		${SRC_DIR}/simulation/Simulation.cpp
		${SRC_DIR}/util/Logger.cpp
		${SRC_DIR}/util/TrajectoryUtil.cpp
		${SRC_DIR}/scene/SceneDiagnostic.cpp
		${SRC_DIR}/scene/SceneModel.cpp
		${SRC_DIR}/scene/SectionInventory.cpp)
	target_include_directories(test_operationsbuilder PRIVATE ${SRC_DIR})
	target_link_libraries(test_operationsbuilder PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts cppzmq nlohmann_json::nlohmann_json)
	if(EGTRAIN_OPENMP_INCLUDE)
		target_include_directories(test_operationsbuilder PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
	endif()
	if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
		target_compile_options(test_operationsbuilder PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
	endif()
	if(EGTRAIN_OPENMP_LINK_LIBRARIES)
		target_link_libraries(test_operationsbuilder PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
	elseif(OpenMP_CXX_FOUND)
		target_link_libraries(test_operationsbuilder PRIVATE OpenMP::OpenMP_CXX)
	endif()
	add_test(NAME test_operationsbuilder COMMAND test_operationsbuilder)

	add_executable(test_railmlparser
		${SRC_DIR}/tests/test_railmlparser.cpp
		${SRC_DIR}/io/RailMLParser.cpp
		${SRC_DIR}/io/third_party/pugixml.cpp
		${SRC_DIR}/simulation/InitialParameters.cpp)
	target_include_directories(test_railmlparser PRIVATE ${SRC_DIR})
	target_link_libraries(test_railmlparser PRIVATE cppzmq nlohmann_json::nlohmann_json)
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
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_headless_smoke_decode.py)
	add_test(NAME test_scene_tool_diagnostics
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_scene_tool_diagnostics.py $<TARGET_FILE:scene_tool>)
	add_test(NAME test_startup_launch_contract
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/startup_launch_contract.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_startup_launch_contract PROPERTIES TIMEOUT 15)
	add_test(NAME test_scene_drop
		COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Paimpol)
	set_tests_properties(test_scene_drop PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QEGTRAIN_E2E_SCENE_DROP=1;QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scene-drop-test"
		PASS_REGULAR_EXPRESSION "E2E_SCENE_DROP_OK"
		TIMEOUT 60)
	add_test(NAME test_native_startup_without_legacy
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_case3_startup_errors.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_native_startup_without_legacy PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 120)
	add_test(NAME test_runtime_memory_safety
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/runtime_memory_safety_smoke.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_runtime_memory_safety PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 240)
	add_test(NAME test_simulation_determinism
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/simulation_determinism_smoke.py $<TARGET_FILE:QEGTRAIN>)
	set_tests_properties(test_simulation_determinism PROPERTIES TIMEOUT 300)
	add_test(NAME test_gui_autostart_smoke
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/gui_autostart_smoke.py $<TARGET_FILE:QEGTRAIN>)
	# Release completes in seconds; the ceiling exists for the instrumented
	# sanitizer job, which needs room for the full run plus a short liveness hold.
	set_tests_properties(test_gui_autostart_smoke PROPERTIES TIMEOUT 420)
	add_test(NAME test_csv_export_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/csv_export_smoke.sh)
	set_tests_properties(test_csv_export_smoke PROPERTIES TIMEOUT 240)
	add_test(NAME test_lebanon_scene_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/lebanon_scene_smoke.sh)
	set_tests_properties(test_lebanon_scene_smoke PROPERTIES TIMEOUT 360)
	add_test(NAME test_creator_acceptance_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/creator_acceptance_smoke.sh)
	set_tests_properties(test_creator_acceptance_smoke PROPERTIES TIMEOUT 240)
	if(APPLE)
		add_test(NAME test_package_contents_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/package_contents_smoke.sh)
		set_tests_properties(test_package_contents_smoke PROPERTIES TIMEOUT 420)
	endif()
	add_test(NAME test_ci_workflow
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_ci_workflow.py)
	add_test(NAME test_signalling_bounds
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_signalling_bounds.py)
	add_test(NAME test_windows_subsystem_config
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_windows_subsystem_config.py)
	add_test(NAME test_raii_contract
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_raii_contract.py)
	add_test(NAME test_ownership_inventory
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_ownership_inventory.py)
	add_test(NAME test_measure_peak_rss
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_measure_peak_rss.py)

	# Canonical V1 is the product path. Legacy tests cover only converter and
	# compatibility boundaries and can be selected with `ctest -L legacy-compat`.
	set_tests_properties(test_scenevalidator test_scenewriter test_scenebuilder test_operationsbuilder test_scene_tool_diagnostics test_trackpreview
		PROPERTIES LABELS "scene-v1")
	set_tests_properties(test_sceneimporter test_sceneexporter test_headless_smoke_decode
		PROPERTIES LABELS "legacy-compat")
	set_tests_properties(test_scenebundle test_scenecompatibility PROPERTIES LABELS "scene-v2")
endif()

# Install
install(TARGETS QEGTRAIN scene_tool
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
    FRAMEWORK DESTINATION .)
install(DIRECTORY ${SRC_DIR}/Scenes/ DESTINATION share/EGTRAIN/Scenes)
