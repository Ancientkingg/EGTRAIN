cmake_minimum_required(VERSION 3.16)
project(EGTRAIN VERSION 1.0 LANGUAGES CXX)

# Default to an optimised build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(EGTRAIN_ENABLE_SANITIZERS "Enable AddressSanitizer/UndefinedBehaviorSanitizer for local debug checks" OFF)

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

# --- Dependencies ---
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Charts)

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
    ${SRC_DIR}/Capacity.cpp
    ${SRC_DIR}/connectionQGraphicsLineItem.cpp
    ${SRC_DIR}/ConsoleWidget.cpp
    ${SRC_DIR}/dispDecision.cpp
    ${SRC_DIR}/EGTRAIN.cpp
    ${SRC_DIR}/geocoding.cpp
    ${SRC_DIR}/infoQDockWidget.cpp
    ${SRC_DIR}/Infrastructure.cpp
    ${SRC_DIR}/initialParameters.cpp
    ${SRC_DIR}/IOClass.cpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/mainwindow.cpp
    ${SRC_DIR}/myQGraphicsColorizeEffect.cpp
    ${SRC_DIR}/myQGraphicsEllipseItem.cpp
    ${SRC_DIR}/myQGraphicsItem.cpp
    ${SRC_DIR}/myQGraphicsLineItem.cpp
    ${SRC_DIR}/myQGraphicsPixmapItem.cpp
    ${SRC_DIR}/myQGraphicsRectItem.cpp
    ${SRC_DIR}/myQGraphicsScene.cpp
    ${SRC_DIR}/myQGraphicsView.cpp
    ${SRC_DIR}/NumberGenerator.cpp
    ${SRC_DIR}/Optimisation.cpp
    ${SRC_DIR}/Owl.cpp
    ${SRC_DIR}/passengerQGraphicsPixmapItem.cpp
    ${SRC_DIR}/Passengers.cpp
    ${SRC_DIR}/platformQGraphicsItem.cpp
    ${SRC_DIR}/pugixml.cpp
    ${SRC_DIR}/Rescheduling.cpp
    ${SRC_DIR}/RollingStock.cpp
    ${SRC_DIR}/Signalling.cpp
    ${SRC_DIR}/signallingQGraphicsEllipseItem.cpp
    ${SRC_DIR}/Simulation.cpp
    ${SRC_DIR}/SimulationWorker.cpp
    ${SRC_DIR}/TimeFormat.cpp
    ${SRC_DIR}/InputValidation.cpp
    ${SRC_DIR}/timeQProgressBar.cpp
    ${SRC_DIR}/trainQGraphicsItemGroup.cpp
    ${SRC_DIR}/trainQGraphicsPolygonItem.cpp
    ${SRC_DIR}/virtualArcQGraphicsLineItem.cpp
    ${SRC_DIR}/Widget.cpp
    ${SRC_DIR}/SpeedFormat.cpp
    ${SRC_DIR}/DiagramWindow.cpp
    ${SRC_DIR}/TrajectoryUtil.cpp
    ${SRC_DIR}/BlockingTimeDiagram.cpp
    ${SRC_DIR}/VisualPolish.cpp
)

set(EGTRAIN_HEADERS
    ${SRC_DIR}/Capacity.h
    ${SRC_DIR}/connectionQGraphicsLineItem.h
    ${SRC_DIR}/ConsoleWidget.h
    ${SRC_DIR}/dispDecision.h
    ${SRC_DIR}/EGTRAIN.h
    ${SRC_DIR}/geocoding.h
    ${SRC_DIR}/infoQDockWidget.h
    ${SRC_DIR}/Infrastructure.h
    ${SRC_DIR}/initialParameters.h
    ${SRC_DIR}/IOClass.h
    ${SRC_DIR}/mainwindow.h
    ${SRC_DIR}/myQGraphicsColorizeEffect.h
    ${SRC_DIR}/myQGraphicsEllipseItem.h
    ${SRC_DIR}/myQGraphicsItem.h
    ${SRC_DIR}/myQGraphicsLineItem.h
    ${SRC_DIR}/myQGraphicsPixmapItem.h
    ${SRC_DIR}/myQGraphicsRectItem.h
    ${SRC_DIR}/myQGraphicsScene.h
    ${SRC_DIR}/myQGraphicsView.h
    ${SRC_DIR}/NumberGenerator.h
    ${SRC_DIR}/Optimisation.h
    ${SRC_DIR}/Owl.hpp
    ${SRC_DIR}/passengerQGraphicsPixmapItem.h
    ${SRC_DIR}/Passengers.h
    ${SRC_DIR}/platformQGraphicsRectItem.h
    ${SRC_DIR}/portability.h
    ${SRC_DIR}/Rescheduling.h
    ${SRC_DIR}/RollingStock.h
    ${SRC_DIR}/Signalling.h
    ${SRC_DIR}/signallingQGraphicsEllipseItem.h
    ${SRC_DIR}/Simulation.h
    ${SRC_DIR}/SimulationWorker.h
    ${SRC_DIR}/TimeFormat.h
    ${SRC_DIR}/InputValidation.h
    ${SRC_DIR}/timeQProgressBar.h
    ${SRC_DIR}/timeutil.hpp
    ${SRC_DIR}/trainQGraphicsItemGroup.h
    ${SRC_DIR}/trainQGraphicsPolygonItem.h
    ${SRC_DIR}/Util.hpp
    ${SRC_DIR}/virtualArcQGraphicsLineItem.h
    ${SRC_DIR}/Widget.h
    ${SRC_DIR}/SpeedFormat.h
    ${SRC_DIR}/DiagramWindow.h
    ${SRC_DIR}/TrajectoryUtil.h
    ${SRC_DIR}/BlockingTimeDiagram.h
    ${SRC_DIR}/VisualPolish.h
)

set(EGTRAIN_UI
    ${SRC_DIR}/mainwindow.ui
    ${SRC_DIR}/Widget.ui
)

set(EGTRAIN_RESOURCES
    ${SRC_DIR}/mainwindow.qrc
)

# --- Executable ---
add_executable(QEGTRAIN
    ${EGTRAIN_SOURCES}
    ${EGTRAIN_HEADERS}
    ${EGTRAIN_UI}
    ${EGTRAIN_RESOURCES}
)

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
target_compile_definitions(QEGTRAIN PRIVATE EGTRAIN_HAS_ZMQ)
message(STATUS "ZeroMQ + nlohmann_json found - traffic-state sharing enabled")

# Platform-specific settings
if(APPLE)
    set_target_properties(QEGTRAIN PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.egtrain.simulator"
        MACOSX_BUNDLE_BUNDLE_NAME "EGTRAIN"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
    # Copy runtime resources (Input data + GUI image assets loaded via
    # relative paths) into the bundle's Resources in a single POST_BUILD step.
    add_custom_command(TARGET QEGTRAIN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/Input"
            "$<TARGET_BUNDLE_CONTENT_DIR:QEGTRAIN>/Resources/Input"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${SRC_DIR}/train_station.png"
            "${SRC_DIR}/pax_icon.png"
            "${SRC_DIR}/window_icon.jpg"
            "$<TARGET_BUNDLE_CONTENT_DIR:QEGTRAIN>/Resources/"
        COMMENT "Copying Input data and GUI image assets into .app bundle"
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
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:10000000000")
endif()

# --- Unit tests (opt-in) ---
option(EGTRAIN_BUILD_TESTS "Build unit tests" OFF)
if(EGTRAIN_BUILD_TESTS)
    enable_testing()
    add_executable(test_timeformat
        ${SRC_DIR}/tests/test_timeformat.cpp
        ${SRC_DIR}/TimeFormat.cpp)
    add_test(NAME test_timeformat COMMAND test_timeformat)

    add_executable(test_inputvalidation
        ${SRC_DIR}/tests/test_inputvalidation.cpp
        ${SRC_DIR}/InputValidation.cpp)
    add_test(NAME test_inputvalidation COMMAND test_inputvalidation)

    add_executable(test_speedformat
        ${SRC_DIR}/tests/test_speedformat.cpp
        ${SRC_DIR}/SpeedFormat.cpp)
    add_test(NAME test_speedformat COMMAND test_speedformat)

    add_executable(test_trajectory
        ${SRC_DIR}/tests/test_trajectory.cpp
        ${SRC_DIR}/TrajectoryUtil.cpp)
    add_test(NAME test_trajectory COMMAND test_trajectory)

    add_executable(test_blockingtimediagram
        ${SRC_DIR}/tests/test_blockingtimediagram.cpp
        ${SRC_DIR}/BlockingTimeDiagram.cpp)
    add_test(NAME test_blockingtimediagram COMMAND test_blockingtimediagram)

    add_executable(test_visualpolish
        ${SRC_DIR}/tests/test_visualpolish.cpp
        ${SRC_DIR}/VisualPolish.cpp)
    target_link_libraries(test_visualpolish PRIVATE Qt5::Core Qt5::Gui)
    add_test(NAME test_visualpolish COMMAND test_visualpolish)
endif()

# Install
install(TARGETS QEGTRAIN
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
    FRAMEWORK DESTINATION .)
