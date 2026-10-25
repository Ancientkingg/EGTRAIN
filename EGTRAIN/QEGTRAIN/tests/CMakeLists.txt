# The tests. The root CMakeLists.txt includes this file when it builds the tests or a package, so CMAKE_CURRENT_SOURCE_DIR
# is the repository and CMAKE_CURRENT_BINARY_DIR the build folder, in which the tests run. The characterization tests are
# registered in characterization/CMakeLists.txt.

# A build with sanitizers runs several times slower than the time limits below allow. The scale multiplies the limit of
# every test that egtrain_add_test registers; the characterization tests have limits of their own.
set(EGTRAIN_TEST_TIMEOUT_SCALE 1 CACHE STRING "Factor for the time limit of the tests that egtrain_add_test registers")
if(NOT EGTRAIN_TEST_TIMEOUT_SCALE MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "EGTRAIN_TEST_TIMEOUT_SCALE must be a positive whole number")
endif()

# The offscreen Qt platform has no fonts of its own on Windows, so GUI tests read the system fonts.
if(WIN32)
	file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" egtrain_windows_fonts)
endif()

# Every test that egtrain_add_test builds links Qt Test (docs/development/testing.md).
find_package(Qt6 REQUIRED COMPONENTS Test)

# egtrain_add_test(<name> LABELS <label>...
#                  [SOURCES <file>... [LIBS <library>...] [DEFINITIONS <definition>...] [SIMULATION_HEADERS]]
#                  [COMMAND <program> <argument>...] [ENVIRONMENT <variable>=<value>...] [TIMEOUT <seconds>] [COST <cost>]
#                  [SKIP_RETURN_CODE <code>] [PASS_REGULAR_EXPRESSION <expression>...]
#                  [OWN_SETTINGS] [OWN_OUTPUT] [WINDOWS_PLATFORM])
# registers the CTest test <name>. SOURCES builds the executable <name>, which links Qt Test and LIBS, compiles with
# DEFINITIONS and is a strict target. The simulation headers still give warnings (#453, #454, #482), so for a test with
# SIMULATION_HEADERS, which includes them, the headers below SRC_DIR are system headers and their warnings do not count.
# COMMAND is the command line of the test, by default the executable <name>. The labels:
#   unit           runs in-process or is a static check
#   integration    starts or inspects a built program (QEGTRAIN, scene_tool, the update helper or a copy of itself), or
#                  talks to a loopback server
#   gui            needs a Qt platform plugin. The test runs on the offscreen platform, which reads the system fonts on
#                  Windows; WINDOWS_PLATFORM keeps the windows platform of Qt on Windows.
#   slow           takes more than 20 seconds on a development machine
#   network        sends a request to a server on the internet
#   scene-v1, scene-v2, legacy-compat
#                  the scene tests, see docs/development/testing.md
# A test has exactly one of unit and integration. OWN_SETTINGS gives a test that starts the application with its user
# settings, which hold the list of recent cases, a settings folder of its own, so that tests can run side by side without
# sharing or changing the settings of the user. OWN_OUTPUT gives it an output folder of its own; without one the
# application writes its run output below the application data directory of the user.
function(egtrain_add_test name)
	cmake_parse_arguments(ARG "SIMULATION_HEADERS;OWN_SETTINGS;OWN_OUTPUT;WINDOWS_PLATFORM" "TIMEOUT;COST;SKIP_RETURN_CODE"
		"SOURCES;LIBS;DEFINITIONS;COMMAND;LABELS;ENVIRONMENT;PASS_REGULAR_EXPRESSION" ${ARGN})
	if(ARG_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "egtrain_add_test(${name}) does not know the arguments ${ARG_UNPARSED_ARGUMENTS}")
	endif()
	set(kinds 0)
	foreach(label IN LISTS ARG_LABELS)
		if(label MATCHES "^(unit|integration)$")
			math(EXPR kinds "${kinds} + 1")
		elseif(NOT label MATCHES "^(gui|slow|network|scene-v1|scene-v2|legacy-compat)$")
			message(FATAL_ERROR "Test ${name} has the unknown label ${label}")
		endif()
	endforeach()
	if(NOT kinds EQUAL 1)
		message(FATAL_ERROR "Test ${name} needs exactly one of the labels unit and integration")
	endif()

	if(ARG_SOURCES)
		add_executable(${name} ${ARG_SOURCES})
		target_link_libraries(${name} PRIVATE Qt::Test ${ARG_LIBS})
		if(ARG_DEFINITIONS)
			target_compile_definitions(${name} PRIVATE ${ARG_DEFINITIONS})
		endif()
		if(ARG_SIMULATION_HEADERS)
			target_include_directories(${name} SYSTEM PRIVATE ${SRC_DIR})
		endif()
		egtrain_target_warnings(${name} STRICT)
	endif()
	if(NOT ARG_COMMAND)
		set(ARG_COMMAND ${name})
	endif()
	add_test(NAME ${name} COMMAND ${ARG_COMMAND})

	set(environment ${ARG_ENVIRONMENT})
	if("gui" IN_LIST ARG_LABELS AND NOT (WIN32 AND ARG_WINDOWS_PLATFORM))
		list(APPEND environment "QT_QPA_PLATFORM=offscreen")
	endif()
	if(ARG_OWN_SETTINGS)
		list(APPEND environment "QEGTRAIN_E2E_SETTINGS_DIR=${CMAKE_CURRENT_BINARY_DIR}/settings/${name}")
	endif()
	if(ARG_OWN_OUTPUT)
		list(APPEND environment "QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/output/${name}")
	endif()
	if(EGTRAIN_TEST_TIMEOUT_SCALE GREATER 1)
		# Scripts with waits of their own, such as startup_launch_contract.py, scale them by the same factor.
		list(APPEND environment "EGTRAIN_TEST_TIMEOUT_SCALE=${EGTRAIN_TEST_TIMEOUT_SCALE}")
		if(ARG_TIMEOUT)
			math(EXPR ARG_TIMEOUT "${ARG_TIMEOUT} * ${EGTRAIN_TEST_TIMEOUT_SCALE}")
		endif()
	endif()
	if(WIN32 AND "gui" IN_LIST ARG_LABELS)
		list(APPEND environment "QT_QPA_FONTDIR=${egtrain_windows_fonts}")
	endif()

	set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABELS}")
	if(environment)
		set_tests_properties(${name} PROPERTIES ENVIRONMENT "${environment}")
	endif()
	foreach(property TIMEOUT COST SKIP_RETURN_CODE PASS_REGULAR_EXPRESSION)
		if(DEFINED ARG_${property})
			set_tests_properties(${name} PROPERTIES ${property} "${ARG_${property}}")
		endif()
	endforeach()
endfunction()

egtrain_add_test(test_updatepreparation
	SOURCES ${SRC_DIR}/tests/test_updatepreparation.cpp
	LIBS egtrain_update Qt::Core
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS unit)

# Stand-ins for an installed application, which the update helper test installs and relaunches.
add_executable(egtrain_update_probe ${SRC_DIR}/tests/update_probe.cpp)
egtrain_target_warnings(egtrain_update_probe STRICT)
set(UPDATEHELPER_TEST_DEFINITIONS
	EGTRAIN_UPDATE_HELPER="$<TARGET_FILE:egtrain_update_helper>"
	EGTRAIN_UPDATE_PROBE="$<TARGET_FILE:egtrain_update_probe>")
set(UPDATEHELPER_TEST_PROGRAMS egtrain_update_helper egtrain_update_probe)
if(WIN32)
	add_library(egtrain_update_probe_dll SHARED ${SRC_DIR}/tests/update_probe_dll.cpp)
	add_executable(egtrain_update_dllprobe ${SRC_DIR}/tests/update_dllprobe.cpp)
	target_link_libraries(egtrain_update_dllprobe PRIVATE egtrain_update_probe_dll)
	egtrain_target_warnings(egtrain_update_probe_dll STRICT)
	egtrain_target_warnings(egtrain_update_dllprobe STRICT)
	list(APPEND UPDATEHELPER_TEST_DEFINITIONS
		EGTRAIN_UPDATE_DLLPROBE="$<TARGET_FILE:egtrain_update_dllprobe>"
		EGTRAIN_UPDATE_PROBE_DLL="$<TARGET_FILE:egtrain_update_probe_dll>")
	list(APPEND UPDATEHELPER_TEST_PROGRAMS egtrain_update_dllprobe egtrain_update_probe_dll)
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
egtrain_add_test(test_updatehelper
	SOURCES ${SRC_DIR}/tests/test_updatehelper.cpp
	LIBS egtrain_update Qt::Core
	DEFINITIONS ${UPDATEHELPER_TEST_DEFINITIONS}
	LABELS integration
	TIMEOUT 120)
add_dependencies(test_updatehelper ${UPDATEHELPER_TEST_PROGRAMS})

# One real HTTPS request to GitHub through the update transport. The package job runs the program from a
# copy of the package; the test test_updatetransport_https below runs it from the build.
add_executable(test_updatetransport_https ${SRC_DIR}/tests/test_updatetransport_https.cpp)
target_link_libraries(test_updatetransport_https PRIVATE egtrain_update Qt::Core Qt::Network)
egtrain_target_warnings(test_updatetransport_https STRICT)

# A packaged build without the tests registers the update tests above, which the package jobs run.
if(NOT EGTRAIN_BUILD_TESTS)
	return()
endif()

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
message(STATUS "Python3: ${Python3_EXECUTABLE}")
egtrain_add_test(test_timeformat
	SOURCES ${SRC_DIR}/tests/test_timeformat.cpp
	LIBS egtrain_util
	LABELS unit)

egtrain_add_test(test_version
	SOURCES ${SRC_DIR}/tests/test_version.cpp
	LIBS egtrain_util
	LABELS unit)

egtrain_add_test(test_log
	SOURCES ${SRC_DIR}/tests/test_log.cpp
	LIBS egtrain_util
	LABELS unit)

# The telemetry tests link egtrain_telemetry_hooks, which defines the test hooks for them.
# The dialog is compiled here, with all hooks, and in the application, without the consent hook.
egtrain_add_test(test_telemetryconsent
	SOURCES ${SRC_DIR}/tests/test_telemetryconsent.cpp ${SRC_DIR}/telemetry/TelemetryConsentDialog.cpp
	LIBS egtrain_telemetry_hooks egtrain_update Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_telemetryqueue
	SOURCES ${SRC_DIR}/tests/test_telemetryqueue.cpp
	LIBS egtrain_telemetry_hooks Qt::Core
	LABELS unit)

egtrain_add_test(test_telemetrysender
	SOURCES ${SRC_DIR}/tests/test_telemetrysender.cpp
	LIBS egtrain_telemetry_hooks Qt::Core Qt::Gui Qt::Network
	LABELS unit)

egtrain_add_test(test_telemetryoperations
	SOURCES ${SRC_DIR}/tests/test_telemetryoperations.cpp
	LIBS egtrain_telemetry_hooks Qt::Widgets Qt::Network
	LABELS unit gui)

add_executable(test_telemetrynetwork ${SRC_DIR}/tests/test_telemetrynetwork.cpp)
target_link_libraries(test_telemetrynetwork PRIVATE Qt::Test egtrain_telemetry_hooks Qt::Core Qt::Gui Qt::Network)
egtrain_target_warnings(test_telemetrynetwork STRICT)
# The sender sends through the HTTP transport, which is WinHTTP on Windows, so the tests need no Qt TLS library there.
foreach(mode reject trusted)
	egtrain_add_test(test_telemetrynetwork_${mode}
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/telemetry_tls_receiver.py
			$<TARGET_FILE:test_telemetrynetwork>
			${SRC_DIR}/tests/fixtures/telemetry-loopback-cert.pem
			${SRC_DIR}/tests/fixtures/telemetry-loopback-key.pem
			${SRC_DIR}/tests/fixtures/telemetry-loopback-ca.pem
			${mode}
		LABELS integration
		TIMEOUT 75)
endforeach()
# Secure Transport on macOS does not trust the root certificate of a request, and the program then returns 77.
set_tests_properties(test_telemetrynetwork_trusted PROPERTIES SKIP_RETURN_CODE 77)

# The controller is compiled here with the dialogs and the self updater that the application alone uses.
egtrain_add_test(test_updatetelemetrycontroller
	SOURCES
		${SRC_DIR}/tests/test_updatetelemetrycontroller.cpp
		${SRC_DIR}/app/UpdateTelemetryController.cpp
		${SRC_DIR}/telemetry/TelemetryConsentDialog.cpp
		${SRC_DIR}/update/SelfUpdater.cpp
	LIBS egtrain_telemetry egtrain_update egtrain_scene egtrain_util Qt::Widgets Qt::Network
	DEFINITIONS EGTRAIN_UPDATE_TEST_HOOK=1
	LABELS unit gui)

egtrain_add_test(test_updatechecker
	SOURCES ${SRC_DIR}/tests/test_updatechecker.cpp
	LIBS egtrain_update egtrain_util Qt::Core Qt::Network
	LABELS unit)

egtrain_add_test(test_httptransport
	SOURCES ${SRC_DIR}/tests/test_httptransport.cpp
	LIBS egtrain_update Qt::Core Qt::Network
	LABELS integration
	TIMEOUT 60)

# The program that asks GitHub for the release list, against a server on the loopback address.
egtrain_add_test(test_updatetransport_https_cases
	COMMAND ${Python3_EXECUTABLE}
		${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_updatetransport_https_cases.py $<TARGET_FILE:test_updatetransport_https>
	LABELS integration
	TIMEOUT 300)
# The request to GitHub needs internet access, so no test run includes it unless the configuration asks for it.
# The program returns 77 when GitHub answers with its rate limit or an outage, which CTest shows as skipped.
if(EGTRAIN_NETWORK_TESTS)
	egtrain_add_test(test_updatetransport_https LABELS integration network TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# SelfUpdater is compiled here with its test hook, which points it at a server on the loopback
# address and at folders of the test. The update probe stands in for the helper.
egtrain_add_test(test_selfupdater
	SOURCES ${SRC_DIR}/tests/test_selfupdater.cpp ${SRC_DIR}/update/SelfUpdater.cpp
	LIBS egtrain_update Qt::Core Qt::Network
	DEFINITIONS EGTRAIN_UPDATE_TEST_HOOK=1 EGTRAIN_UPDATE_PROBE="$<TARGET_FILE:egtrain_update_probe>"
	LABELS integration
	TIMEOUT 120)
add_dependencies(test_selfupdater egtrain_update_probe)

egtrain_add_test(test_updatestaging
	SOURCES ${SRC_DIR}/tests/test_updatestaging.cpp
	LIBS Qt::Core
	LABELS unit)

egtrain_add_test(test_updaterecord
	SOURCES ${SRC_DIR}/tests/test_updaterecord.cpp
	LIBS egtrain_update Qt::Core
	LABELS unit)

egtrain_add_test(test_distribution
	SOURCES ${SRC_DIR}/tests/test_distribution.cpp
	LIBS egtrain_update Qt::Core
	LABELS unit)

egtrain_add_test(test_recoverystore
	SOURCES ${SRC_DIR}/tests/test_recoverystore.cpp
	LIBS egtrain_recovery Qt::Core
	LABELS unit)
# The same program, started again as the child that holds or loses a copy.
egtrain_add_test(test_recoverystore_process
	COMMAND test_recoverystore --processes
	LABELS integration
	TIMEOUT 120)

egtrain_add_test(test_recoverycontroller
	SOURCES ${SRC_DIR}/tests/test_recoverycontroller.cpp
	LIBS egtrain_recovery Qt::Core
	LABELS unit
	TIMEOUT 120)

# Starts itself as a child that crashes on purpose; the system may keep a crash report of each child.
egtrain_add_test(test_crashnote
	SOURCES ${SRC_DIR}/tests/test_crashnote.cpp ${SRC_DIR}/app/CrashNote.cpp
	LIBS Qt::Core Threads::Threads
	LABELS integration
	TIMEOUT 300)

egtrain_add_test(test_recoveryprompt
	SOURCES ${SRC_DIR}/tests/test_recoveryprompt.cpp
	LIBS egtrain_recovery Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_guisimulationsnapshot SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_guisimulationsnapshot.cpp
	LIBS egtrain_dispatch
	LABELS unit)

# Runs the line fixture in this process and compares the live frames of its snapshots with the golden files.
egtrain_add_test(test_liveframe SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_liveframe.cpp
	LIBS egtrain_live egtrain_dispatch egtrain_sim Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
		EGTRAIN_LIVE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/docs/live/frame-v1.schema.json"
	LABELS unit
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 120)

# Checks the requests of the command socket and the event frame that reports a command. No socket, thread or run.
egtrain_add_test(test_livecommand
	SOURCES ${SRC_DIR}/tests/test_livecommand.cpp
	LIBS egtrain_live nlohmann_json::nlohmann_json
	LABELS unit
	TIMEOUT 120)

# Replays the commands of a live client against the line fixture in this process, and compares the results with
# those of a run without commands.
egtrain_add_test(test_livereplay SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_livereplay.cpp
	LIBS egtrain_live egtrain_dispatch egtrain_sim cppzmq Qt::Core Qt::Gui Qt::Widgets
	COMMAND test_livereplay
		--fixture ${SRC_DIR}/tests/fixtures/scenes/line
		--script ${SRC_DIR}/tests/live/commands
	LABELS integration
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 180)

# Starts the live server in this process and talks to it through ZeroMQ sockets on the loopback address.
egtrain_add_test(test_liveserver
	SOURCES ${SRC_DIR}/tests/test_liveserver.cpp
	LIBS egtrain_live cppzmq nlohmann_json::nlohmann_json Threads::Threads
	LABELS unit
	TIMEOUT 120)

# The small client of the live stream. test_live_stream_smoke runs it beside the application.
add_executable(live_client ${SRC_DIR}/tests/live_client.cpp)
target_link_libraries(live_client PRIVATE cppzmq nlohmann_json::nlohmann_json)
egtrain_target_warnings(live_client STRICT)

egtrain_add_test(test_followavailability
	SOURCES ${SRC_DIR}/tests/test_followavailability.cpp
	LIBS egtrain_util
	LABELS unit)

egtrain_add_test(test_replaycoverage
	SOURCES ${SRC_DIR}/tests/test_replaycoverage.cpp
	LIBS egtrain_dispatch egtrain_util
	LABELS unit)

egtrain_add_test(test_trackpreview
	SOURCES ${SRC_DIR}/tests/test_trackpreview.cpp
	LIBS egtrain_scene
	LABELS scene-v1 unit)

egtrain_add_test(test_speedformat
	SOURCES ${SRC_DIR}/tests/test_speedformat.cpp
	LIBS egtrain_util
	LABELS unit)

egtrain_add_test(test_trajectory
	SOURCES ${SRC_DIR}/tests/test_trajectory.cpp
	LIBS egtrain_util
	LABELS unit)

egtrain_add_test(test_runresults SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_runresults.cpp
	LIBS egtrain_diagrams egtrain_sim Qt::Core Qt::Gui Qt::Widgets Qt::Charts
	LABELS unit)

egtrain_add_test(test_delayanalysis
	SOURCES ${SRC_DIR}/tests/test_delayanalysis.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_delayanalysiswindow
	SOURCES ${SRC_DIR}/tests/test_delayanalysiswindow.cpp
	LIBS egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt::Network Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_simulationworker SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_simulationworker.cpp
	LIBS egtrain_dispatch egtrain_sim Qt::Core Qt::Gui Qt::Widgets Qt::Charts
	DEFINITIONS EGTRAIN_SCENES_DIR="${SRC_DIR}/Scenes"
	LABELS unit)

egtrain_add_test(test_blockingtimediagram
	SOURCES ${SRC_DIR}/tests/test_blockingtimediagram.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_capacityanalysis
	SOURCES ${SRC_DIR}/tests/test_capacityanalysis.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_areacapacity
	SOURCES ${SRC_DIR}/tests/test_areacapacity.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_areacapacitywindow
	SOURCES ${SRC_DIR}/tests/test_areacapacitywindow.cpp
	LIBS egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt::Network Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_capacitywindow
	SOURCES ${SRC_DIR}/tests/test_capacitywindow.cpp
	LIBS egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt::Network Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_routediagramcoordinates
	SOURCES ${SRC_DIR}/tests/test_routediagramcoordinates.cpp
	LIBS egtrain_diagrams egtrain_util
	LABELS unit)

egtrain_add_test(test_positionscale
	SOURCES ${SRC_DIR}/tests/test_positionscale.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_traincolours
	SOURCES ${SRC_DIR}/tests/test_traincolours.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_routegeo SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_routegeo.cpp
	LIBS egtrain_diagrams egtrain_sim
	COMMAND test_routegeo ${SRC_DIR}/Scenes
	LABELS unit)

egtrain_add_test(test_diagramwindow
	SOURCES ${SRC_DIR}/tests/test_diagramwindow.cpp
	LIBS egtrain_diagrams egtrain_telemetry egtrain_util Qt::Network Qt::Core Qt::Gui Qt::Widgets Qt::Charts
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_timetabletablewindow
	SOURCES ${SRC_DIR}/tests/test_timetabletablewindow.cpp
	LIBS egtrain_diagrams egtrain_telemetry egtrain_scene egtrain_util Qt::Network Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_visualpolish
	SOURCES ${SRC_DIR}/tests/test_visualpolish.cpp
	LIBS egtrain_graphics Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_networklegend
	SOURCES ${SRC_DIR}/tests/test_networklegend.cpp
	LIBS egtrain_widgets egtrain_graphics Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_stationoverlay
	SOURCES ${SRC_DIR}/tests/test_stationoverlay.cpp
	LIBS egtrain_graphics Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_networkscene_selection
	SOURCES ${SRC_DIR}/tests/test_networkscene_selection.cpp
	LIBS egtrain_graphics Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_playbackprofiler
	SOURCES ${SRC_DIR}/tests/test_playbackprofiler.cpp
	LIBS egtrain_util
	LABELS unit)
egtrain_add_test(test_playback_profile
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/performance/test_playback_profile.py
	LABELS unit)

egtrain_add_test(test_networkview
	SOURCES ${SRC_DIR}/tests/test_networkview.cpp
	LIBS egtrain_graphics egtrain_util Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_networkpresenter
	SOURCES ${SRC_DIR}/tests/test_networkpresenter.cpp
	LIBS egtrain_graphics Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_inspectorpanel SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_inspectorpanel.cpp
	LIBS egtrain_widgets egtrain_graphics egtrain_sim Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_runtimecanvas SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_runtimecanvas.cpp
	LIBS egtrain_graphics egtrain_sim Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_followcamera
	SOURCES ${SRC_DIR}/tests/test_followcamera.cpp
	LIBS egtrain_graphics egtrain_util Qt::Core Qt::Gui Qt::Widgets cppzmq nlohmann_json::nlohmann_json
	LABELS unit gui)

egtrain_add_test(test_compactdoublespinbox
	SOURCES ${SRC_DIR}/tests/test_compactdoublespinbox.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_lightpalette
	SOURCES ${SRC_DIR}/tests/test_lightpalette.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

# On Windows the test needs the style that the windows platform gives.
egtrain_add_test(test_widgetstyle
	SOURCES ${SRC_DIR}/tests/test_widgetstyle.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui
	WINDOWS_PLATFORM)

egtrain_add_test(test_colorchoicebutton
	SOURCES ${SRC_DIR}/tests/test_colorchoicebutton.cpp
	LIBS egtrain_widgets Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_aboutdialog
	SOURCES ${SRC_DIR}/tests/test_aboutdialog.cpp
	LIBS egtrain_widgets Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

# The Settings window uses the update and telemetry libraries, so it is compiled here and in the application.
egtrain_add_test(test_settingsdialog
	SOURCES ${SRC_DIR}/tests/test_settingsdialog.cpp ${SRC_DIR}/widgets/SettingsDialog.cpp
	LIBS egtrain_telemetry egtrain_update Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_manualwindow
	SOURCES ${SRC_DIR}/tests/test_manualwindow.cpp
	LIBS egtrain_widgets Qt::Core Qt::Gui Qt::Widgets Qt::Help
	DEFINITIONS EGTRAIN_QHELPGENERATOR="$<TARGET_FILE:Qt6::qhelpgenerator>"
	LABELS unit gui)

egtrain_add_test(test_dialoglayout
	SOURCES ${SRC_DIR}/tests/test_dialoglayout.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_casechooserdialog
	SOURCES ${SRC_DIR}/tests/test_casechooserdialog.cpp
	LIBS egtrain_widgets Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_choicecombo
	SOURCES ${SRC_DIR}/tests/test_choicecombo.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_userprompts
	SOURCES ${SRC_DIR}/tests/test_userprompts.cpp
	LIBS egtrain_util Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_guardedapplication
	SOURCES ${SRC_DIR}/tests/test_guardedapplication.cpp ${SRC_DIR}/app/GuardedApplication.cpp
	LIBS egtrain_util Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui
	SKIP_RETURN_CODE 77)

egtrain_add_test(test_editor_casesettings
	SOURCES ${SRC_DIR}/tests/test_editor_casesettings.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_composition
	SOURCES ${SRC_DIR}/tests/test_editor_composition.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_trainunit
	SOURCES ${SRC_DIR}/tests/test_editor_trainunit.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_trainunitsourcelinker
	SOURCES ${SRC_DIR}/tests/test_trainunitsourcelinker.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_passenger
	SOURCES ${SRC_DIR}/tests/test_editor_passenger.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_service
	SOURCES ${SRC_DIR}/tests/test_editor_service.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_timetablestops
	SOURCES ${SRC_DIR}/tests/test_editor_timetablestops.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_scenario
	SOURCES ${SRC_DIR}/tests/test_editor_scenario.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_validation
	SOURCES ${SRC_DIR}/tests/test_editor_validation.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_incident
	SOURCES ${SRC_DIR}/tests/test_editor_incident.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_entrancedelay
	SOURCES ${SRC_DIR}/tests/test_editor_entrancedelay.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_infrastructure
	SOURCES ${SRC_DIR}/tests/test_editor_infrastructure.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_infrastructure_blocks
	SOURCES ${SRC_DIR}/tests/test_editor_infrastructure_blocks.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_infrastructure_signals
	SOURCES ${SRC_DIR}/tests/test_editor_infrastructure_signals.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_infrastructure_stations
	SOURCES ${SRC_DIR}/tests/test_editor_infrastructure_stations.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_editor_infrastructure_topology
	SOURCES ${SRC_DIR}/tests/test_editor_infrastructure_topology.cpp
	LIBS egtrain_editors Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_infrastructureguidance
	SOURCES ${SRC_DIR}/tests/test_infrastructureguidance.cpp
	LIBS egtrain_editors Qt::Core
	LABELS unit)

egtrain_add_test(test_editorlayout
	SOURCES ${SRC_DIR}/tests/test_editorlayout.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_DIALOG_QSS="${SRC_DIR}/resources/styles/egtrain.qss"
	LABELS unit gui)

egtrain_add_test(test_elidedlabel
	SOURCES ${SRC_DIR}/tests/test_elidedlabel.cpp
	LIBS Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_timeprogressbar
	SOURCES ${SRC_DIR}/tests/test_timeprogressbar.cpp
	LIBS egtrain_widgets egtrain_util Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_highlighteffect
	SOURCES ${SRC_DIR}/tests/test_highlighteffect.cpp
	LIBS egtrain_graphics Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_csvwriter
	SOURCES ${SRC_DIR}/tests/test_csvwriter.cpp
	LIBS egtrain_util
	LABELS unit)

egtrain_add_test(test_tractioncurve
	SOURCES ${SRC_DIR}/tests/test_tractioncurve.cpp
	LIBS egtrain_diagrams
	LABELS unit)

egtrain_add_test(test_consolestream
	SOURCES ${SRC_DIR}/tests/test_consolestream.cpp
	LIBS egtrain_widgets Qt::Core Qt::Gui Qt::Widgets
	LABELS unit)

egtrain_add_test(test_qt_resources
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/e2e/test_qt_resources.py
	LABELS unit)
egtrain_add_test(test_app_icon_config
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/e2e/test_app_icon_config.py
	LABELS unit)
egtrain_add_test(test_check_docs
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/test_check_docs.py
	LABELS unit)
egtrain_add_test(test_check_undocumented
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/test_check_undocumented.py
	LABELS unit)
# The documentation check reads the file list from Git, so it runs only in a checkout.
if(EXISTS ${CMAKE_SOURCE_DIR}/.git)
	egtrain_add_test(docs_check
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/check_docs.py --root ${CMAKE_SOURCE_DIR}
		LABELS unit)
endif()
egtrain_add_test(test_qt_resource_images
	SOURCES ${SRC_DIR}/tests/test_qt_resource_images.cpp ${EGTRAIN_RESOURCES}
	LIBS Qt::Core Qt::Gui Qt::Svg
	LABELS unit gui)

egtrain_add_test(test_routestations
	SOURCES ${SRC_DIR}/tests/test_routestations.cpp
	LIBS egtrain_scene
	COMMAND test_routestations ${SRC_DIR}/Scenes
	LABELS scene-v1 unit)

egtrain_add_test(test_routereferencechoice
	SOURCES ${SRC_DIR}/tests/test_routereferencechoice.cpp
	LIBS egtrain_diagrams egtrain_telemetry egtrain_scene Qt::Network Qt::Core Qt::Gui Qt::Widgets
	COMMAND test_routereferencechoice ${SRC_DIR}/Scenes
	LABELS unit gui)

egtrain_add_test(test_scenevalidator
	SOURCES ${SRC_DIR}/tests/test_scenevalidator.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS scene-v1 unit)

egtrain_add_test(test_stopinsertion
	SOURCES ${SRC_DIR}/tests/test_stopinsertion.cpp
	LIBS egtrain_scene
	LABELS scene-v1 unit)

egtrain_add_test(test_scenereferences
	SOURCES ${SRC_DIR}/tests/test_scenereferences.cpp
	LIBS egtrain_scene
	LABELS scene-v1 unit)

egtrain_add_test(test_scenedocument
	SOURCES ${SRC_DIR}/tests/test_scenedocument.cpp
	LIBS egtrain_editors
	LABELS unit)

egtrain_add_test(test_runselection
	SOURCES ${SRC_DIR}/tests/test_runselection.cpp
	LIBS egtrain_runselection
	LABELS unit)

egtrain_add_test(test_sceneimporter
	SOURCES ${SRC_DIR}/tests/test_sceneimporter.cpp
	LIBS egtrain_scene
	LABELS legacy-compat unit)

egtrain_add_test(test_scenesavepolicy
	SOURCES ${SRC_DIR}/tests/test_scenesavepolicy.cpp
	LIBS Qt::Core
	LABELS scene-v1 unit)

egtrain_add_test(test_dialogfolders
	SOURCES ${SRC_DIR}/tests/test_dialogfolders.cpp
	LIBS Qt::Core
	LABELS unit)

egtrain_add_test(test_cannotopenmessage
	SOURCES ${SRC_DIR}/tests/test_cannotopenmessage.cpp
	LIBS Qt::Core
	LABELS unit)

egtrain_add_test(test_pathmessages
	SOURCES ${SRC_DIR}/tests/test_pathmessages.cpp
	LIBS Qt::Core
	LABELS unit)

egtrain_add_test(test_updateprompttext
	SOURCES ${SRC_DIR}/tests/test_updateprompttext.cpp
	LIBS Qt::Core
	LABELS unit)

egtrain_add_test(test_releasenotes
	SOURCES ${SRC_DIR}/tests/test_releasenotes.cpp
	LIBS egtrain_update Qt::Core Qt::Gui Qt::Widgets
	LABELS unit gui)

egtrain_add_test(test_scenewriter
	SOURCES ${SRC_DIR}/tests/test_scenewriter.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests" EGTRAIN_SCENES_DIR="${SRC_DIR}/Scenes"
	LABELS scene-v1 unit)

egtrain_add_test(test_sceneexporter
	SOURCES ${SRC_DIR}/tests/test_sceneexporter.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS legacy-compat unit)

egtrain_add_test(test_scenebundle
	SOURCES ${SRC_DIR}/tests/test_scenebundle.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS scene-v2 unit)

egtrain_add_test(test_scenehistory
	SOURCES ${SRC_DIR}/tests/test_scenehistory.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_SCENES_DIR="${SRC_DIR}/Scenes"
	LABELS unit)

egtrain_add_test(test_sceneopenrequest
	SOURCES ${SRC_DIR}/tests/test_sceneopenrequest.cpp
	LIBS egtrain_sceneopen Qt::Core
	LABELS unit)

# The test starts copies of itself as the second process.
egtrain_add_test(test_openrequestrelay
	SOURCES ${SRC_DIR}/tests/test_openrequestrelay.cpp
	LIBS egtrain_openrelay Qt::Core Qt::Network
	LABELS integration
	TIMEOUT 120)

egtrain_add_test(test_scenecompatibility
	SOURCES ${SRC_DIR}/tests/test_scenecompatibility.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS scene-v2 unit)

egtrain_add_test(test_sceneversions
	SOURCES ${SRC_DIR}/tests/test_sceneversions.cpp
	LIBS egtrain_scene
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS scene-v2 unit)

egtrain_add_test(test_scenebuilder SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_scenebuilder.cpp
	LIBS egtrain_sim
	LABELS scene-v1 unit)

egtrain_add_test(test_stopdwell SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_stopdwell.cpp
	LIBS egtrain_sim
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS scene-v1 unit)

egtrain_add_test(test_operationsbuilder SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_operationsbuilder.cpp
	LIBS egtrain_diagrams egtrain_sim Qt::Core Qt::Gui Qt::Widgets Qt::Charts
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests"
	LABELS scene-v1 unit)

# Runs three committed scenes in this process, like the characterization tests.
egtrain_add_test(test_completedrun SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_completedrun.cpp
	LIBS egtrain_dispatch egtrain_diagrams egtrain_telemetry egtrain_sim Qt::Core Qt::Gui Qt::Widgets Qt::Charts
	COMMAND test_completedrun ${SRC_DIR}/Scenes/Assignment_Gvc_Gdg_Ut ${SRC_DIR}/Scenes/Paimpol ${SRC_DIR}/Scenes/Milano_Brescia
	LABELS unit gui
	TIMEOUT 300)

# Runs a line fixture and Paimpol in this process, in the order that the test explains.
egtrain_add_test(test_simulationrun SIMULATION_HEADERS
	SOURCES ${SRC_DIR}/tests/test_simulationrun.cpp
	LIBS egtrain_dispatch egtrain_sim Qt::Core Qt::Gui Qt::Widgets
	DEFINITIONS EGTRAIN_TEST_SOURCE_DIR="${SRC_DIR}/tests" EGTRAIN_SCENES_DIR="${SRC_DIR}/Scenes"
	LABELS unit
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 120)

# Behaviour characterization of the simulation core (see tests/characterization). Its programs include the simulation
# headers and build like a test with SIMULATION_HEADERS.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/characterization)
foreach(egtrain_test test_characterization test_crossover_chain test_signalling_aspects)
	target_include_directories(${egtrain_test} SYSTEM PRIVATE ${SRC_DIR})
	egtrain_target_warnings(${egtrain_test} STRICT)
endforeach()

# Scripted GUI runs, compiled into QEGTRAIN when tests are built.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/e2e)

egtrain_add_test(test_headless_smoke_decode
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_headless_smoke_decode.py
	LABELS legacy-compat unit)
egtrain_add_test(test_scene_tool_diagnostics
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_scene_tool_diagnostics.py $<TARGET_FILE:scene_tool>
	LABELS scene-v1 integration)
# Cover two 60-second child launches, two 8-second PTY checks, a 60-second wait for the window
# launch with a file name and cleanup, plus 30 seconds for the other file name launches. The
# launches that hand a file to a running window add 60 seconds for the window to start, 60 seconds
# for a file to arrive and 60 seconds for a scripted launch.
egtrain_add_test(test_startup_launch_contract
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/startup_launch_contract.py $<TARGET_FILE:QEGTRAIN> $<TARGET_FILE:scene_tool>
	LABELS integration gui
	TIMEOUT 480
	OWN_SETTINGS)
if(NOT WIN32)
	egtrain_add_test(test_case_chooser_contract
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/case_chooser_contract.py
			$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/Scenes/Paimpol
		LABELS integration gui
		TIMEOUT 120)
else()
	message(STATUS "Windows: skipping test_case_chooser_contract (path forms, INI escapes, directory symlink)")
endif()
egtrain_add_test(test_scene_drop
	COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Paimpol
	LABELS integration gui
	ENVIRONMENT "QEGTRAIN_E2E_SCENE_DROP=1;QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scene-drop-test"
	PASS_REGULAR_EXPRESSION "E2E_SCENE_DROP_OK"
	TIMEOUT 60
	OWN_SETTINGS)
egtrain_add_test(test_output_folder
	COMMAND $<TARGET_FILE:QEGTRAIN> --scene ${SRC_DIR}/Scenes/Paimpol
	LABELS integration gui
	ENVIRONMENT "QEGTRAIN_E2E_OUTPUT_FOLDER=1;QEGTRAIN_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/output-folder-test"
	# The run that an exception ends shows Simulation Failed, which a scripted run writes to stderr.
	PASS_REGULAR_EXPRESSION "DIALOG_SUPPRESSED: Simulation Failed.*E2E_OUTPUT_FOLDER_OK;E2E_OUTPUT_FOLDER_OK.*DIALOG_SUPPRESSED: Simulation Failed"
	TIMEOUT 120
	OWN_SETTINGS)
egtrain_add_test(test_native_startup_without_legacy
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_case3_startup_errors.py $<TARGET_FILE:QEGTRAIN>
	LABELS integration
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 120)
egtrain_add_test(test_runtime_memory_safety
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/runtime_memory_safety_smoke.py $<TARGET_FILE:QEGTRAIN>
	LABELS integration
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 240)
egtrain_add_test(test_simulation_determinism
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/simulation_determinism_smoke.py $<TARGET_FILE:QEGTRAIN>
	LABELS integration
	TIMEOUT 300)
# The case in which a second run follows the first in one process uses a scripted run of the window, which this repository
# checks on macOS only (see the visual polish smokes below).
if(APPLE)
	set(egtrain_live_stream_smoke_args --second-run)
endif()
egtrain_add_test(test_live_stream_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/live_stream_smoke.py
		$<TARGET_FILE:QEGTRAIN> $<TARGET_FILE:live_client> ${egtrain_live_stream_smoke_args}
	LABELS integration gui
	TIMEOUT 300)
egtrain_add_test(test_station_departure
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/station_departure_smoke.py $<TARGET_FILE:QEGTRAIN>
	LABELS integration
	TIMEOUT 300)
# Release completes in seconds; the ceiling exists for the instrumented
# sanitizer job, which needs room for the full run plus a short liveness hold.
# The longest tests start first, so that a parallel run does not end waiting for one of them.
# Without the timings of an earlier run, ctest orders tests by this cost.
egtrain_add_test(test_gui_autostart_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/gui_autostart_smoke.py $<TARGET_FILE:QEGTRAIN>
	LABELS integration gui slow
	TIMEOUT 420
	COST 80
	OWN_SETTINGS)
egtrain_add_test(test_close_during_run_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/close_during_run_smoke.py $<TARGET_FILE:QEGTRAIN>
	LABELS integration gui
	TIMEOUT 240
	OWN_SETTINGS)
if(UNIX)
	# The scripts get the built application, scene_tool and a platform plugin that needs no display.
	set(egtrain_script_environment "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>;QEGTRAIN_SCENE_TOOL=$<TARGET_FILE:scene_tool>")
	egtrain_add_test(test_csv_export_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/csv_export_smoke.sh
		LABELS integration gui
		ENVIRONMENT ${egtrain_script_environment}
		TIMEOUT 240
		OWN_SETTINGS)
	egtrain_add_test(test_creator_acceptance_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/creator_acceptance_smoke.sh
		LABELS integration gui
		ENVIRONMENT ${egtrain_script_environment}
		TIMEOUT 240)
	egtrain_add_test(test_editor_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/editor_smoke.sh
		LABELS integration gui
		ENVIRONMENT ${egtrain_script_environment}
		TIMEOUT 300)
	# A window that ends without closing, started again. The lock files of the copies are not checked after a crash on Windows.
	egtrain_add_test(test_recovery_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/recovery_smoke.py $<TARGET_FILE:QEGTRAIN> $<TARGET_FILE:scene_tool>
		LABELS integration gui
		TIMEOUT 600)
	egtrain_add_test(test_dialog_presentation_contract
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/creator_acceptance_smoke.sh --dialog-contract
		LABELS integration gui slow
		ENVIRONMENT ${egtrain_script_environment}
		TIMEOUT 600)
else()
	message(STATUS "Windows: skipping bash smoke tests (test_csv_export_smoke, test_creator_acceptance_smoke, test_editor_smoke, test_dialog_presentation_contract)")
endif()
# The end-to-end smokes of the committed scenes and the route diagram run. A build with sanitizers
# does not register them: the sanitizer job of the release workflow runs all of CTest on an
# instrumented Debug build, and the time limits of these scripts are set for an uninstrumented one.
if(NOT EGTRAIN_ENABLE_SANITIZERS)
	# The scripts get the built application, scene_tool and a platform plugin that needs no display.
	set(egtrain_e2e_environment
		"QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>;QEGTRAIN_SCENE_TOOL=$<TARGET_FILE:scene_tool>;QT_QPA_PLATFORM=offscreen")
	# One test per committed scene, so that they run side by side. Each case writes its own Output folder.
	# Copenhagen takes longest and starts first.
	foreach(egtrain_case 1 2 3 4 5 7)
		egtrain_add_test(test_headless_smoke_case_${egtrain_case}
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_smoke.py ${egtrain_case}
			LABELS integration
			ENVIRONMENT ${egtrain_e2e_environment}
			TIMEOUT 900)
	endforeach()
	set_tests_properties(test_headless_smoke_case_3 PROPERTIES COST 200)
	egtrain_add_test(test_roundtrip_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/roundtrip_smoke.py
		LABELS integration
		ENVIRONMENT ${egtrain_e2e_environment}
		TIMEOUT 300)
	egtrain_add_test(test_bundle_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/bundle_smoke.py $<TARGET_FILE:scene_tool> $<TARGET_FILE:QEGTRAIN>
		LABELS integration
		ENVIRONMENT ${egtrain_e2e_environment}
		TIMEOUT 300)
	egtrain_add_test(test_incident_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/incident_smoke.py
		LABELS integration
		ENVIRONMENT ${egtrain_e2e_environment}
		TIMEOUT 600)
	egtrain_add_test(test_assignment_smoke
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/assignment_smoke.py
		LABELS integration
		ENVIRONMENT ${egtrain_e2e_environment}
		TIMEOUT 30)
	if(UNIX)
		# The scripts get the built application and a platform plugin that needs no display.
		egtrain_add_test(test_scene_render_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/scene_render_smoke.sh
			LABELS integration gui
			ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>"
			TIMEOUT 300
			OWN_SETTINGS OWN_OUTPUT)
		egtrain_add_test(test_track_preview_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/track_preview_smoke.sh
			LABELS integration gui
			ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>"
			TIMEOUT 240
			OWN_SETTINGS OWN_OUTPUT)
		egtrain_add_test(test_legacy_import_smoke
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/legacy_import_smoke.sh
			LABELS integration gui
			ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>"
			TIMEOUT 120
			OWN_SETTINGS OWN_OUTPUT)
		egtrain_add_test(test_route_diagram_smoke
			COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/route_diagram_smoke.py
				$<TARGET_FILE:QEGTRAIN> $<TARGET_FILE_DIR:test_diagramwindow>
			LABELS integration gui
			TIMEOUT 480
			OWN_SETTINGS OWN_OUTPUT)
	else()
		message(STATUS "Windows: skipping the GUI smoke tests (test_scene_render_smoke, test_track_preview_smoke, test_legacy_import_smoke, test_route_diagram_smoke)")
	endif()
	if(APPLE)
		# visual_polish_smoke.sh runs its phases one after another when it gets no argument. The phases write file
		# names of their own below TMPDIR, so CTest runs them as separate tests side by side. The last phase, which
		# runs scene_render_smoke.sh and legacy_import_smoke.sh, has no test of its own: those two are tests above.
		# The cost is about the seconds that the test takes on a development machine, so that the longest start first.
		function(egtrain_add_visual_polish_smoke name cost timeout)
			egtrain_add_test(test_visual_polish_smoke_${name}
				COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/visual_polish_smoke.sh ${ARGN}
				LABELS integration gui slow
				ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>"
				TIMEOUT ${timeout}
				COST ${cost}
				OWN_SETTINGS OWN_OUTPUT)
		endfunction()
		egtrain_add_visual_polish_smoke(replay_copenhagen 100 600 replay-copenhagen)
		egtrain_add_visual_polish_smoke(replay 45 300 completion replay discard)
		egtrain_add_visual_polish_smoke(signals 45 300 signals)
		egtrain_add_visual_polish_smoke(live 35 300 dpr1 color dpr2)
		egtrain_add_visual_polish_smoke(stations 30 240 stations)
		# Takes the pictures of docs/images on the offscreen platform into a temporary folder and checks that every
		# picture of docs/images/captures.json is taken. Sizes and pixels are not compared: the pictures are taken on
		# macOS, and a dialog takes its size from the font and style of the platform.
		egtrain_add_test(test_capture_screenshots
			COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/docs/capture_screenshots.sh --taken
			LABELS integration gui slow
			ENVIRONMENT "QEGTRAIN_APP=$<TARGET_FILE:QEGTRAIN>"
			TIMEOUT 300)
	endif()
endif()
if(APPLE)
	egtrain_add_test(test_package_contents_smoke
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/package_contents_smoke.sh
		LABELS integration gui
		TIMEOUT 420)
	# Signs a fixture bundle with the release signing script. It needs cc and codesign, not the built application.
	egtrain_add_test(test_sign_macos
		COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_sign_macos.sh
		LABELS integration
		TIMEOUT 120)
endif()
egtrain_add_test(test_release_version
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_version.py
	LABELS unit)
egtrain_add_test(test_release_assets
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_release_assets.py
	LABELS unit)
egtrain_add_test(test_make_dmg
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_make_dmg.py
	LABELS unit)
egtrain_add_test(test_macos_build_paths
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_macos_build_paths.py
	LABELS unit)
egtrain_add_test(test_installer_script
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_installer_script.py
	LABELS unit)
egtrain_add_test(test_ci_workflow
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_ci_workflow.py
	LABELS unit)
egtrain_add_test(test_signalling_bounds
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_signalling_bounds.py
	LABELS unit)
egtrain_add_test(test_logging_contract
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_logging_contract.py
	LABELS unit)
egtrain_add_test(test_no_e2e_in_product
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_no_e2e_in_product.py
	LABELS unit)
egtrain_add_test(test_diagrams_no_sim_globals
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_diagrams_no_sim_globals.py
	LABELS unit)
egtrain_add_test(test_scene_codec_contract
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_scene_codec_contract.py
	LABELS scene-v1 unit)
egtrain_add_test(test_windows_subsystem_config
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/test_windows_subsystem_config.py
	LABELS unit)
egtrain_add_test(test_raii_contract
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_raii_contract.py
	LABELS unit)
egtrain_add_test(test_ownership_inventory
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_ownership_inventory.py
	LABELS unit)
if(NOT WIN32)
	egtrain_add_test(test_measure_peak_rss
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/memory/test_measure_peak_rss.py
		LABELS unit)
else()
	message(STATUS "Windows: skipping test_measure_peak_rss (macOS /usr/bin/time collector)")
endif()

# Headless run of a built scene. The scene path is relative to the executable.
if(APPLE)
	set(EGTRAIN_SMOKE_APP_SCENE "../Resources/Scenes/Paimpol")
else()
	set(EGTRAIN_SMOKE_APP_SCENE "Scenes/Paimpol")
endif()
egtrain_add_test(test_headless_scene_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
		$<TARGET_FILE:QEGTRAIN> ${EGTRAIN_SMOKE_APP_SCENE}
	LABELS integration
	TIMEOUT 150)

# The same two runs on each scene of tests/fixtures/scene-versions that is meant to run.
egtrain_add_test(test_scene_version_run_main-82.1
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
		$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/main-82.1-Paimpol.egscene 300
	LABELS integration
	TIMEOUT 150)
egtrain_add_test(test_scene_version_run_production-107.1
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
		$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/production-107.1-Paimpol.egscene 300
	LABELS integration
	TIMEOUT 150)
egtrain_add_test(test_scene_version_run_v1.0.2
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
		$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/v1.0.2-Paimpol.egscene 300
	LABELS integration
	TIMEOUT 150)
egtrain_add_test(test_scene_version_run_main-81.1
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e/headless_scene_smoke.py
		$<TARGET_FILE:QEGTRAIN> ${SRC_DIR}/tests/fixtures/scene-versions/main-81.1-Assignment_Gvc_Gdg_Ut 600
	LABELS integration
	TIMEOUT 150)

# A Windows EXE image near 2 GiB fails to start, and fixed-size static storage is what makes an
# image large. The limit is checked on every Windows build, so such storage is noticed early.
# The stack reserve is checked with it: the application has its own, scene_tool the linker default.
if(WIN32)
	set(EGTRAIN_MAX_PE_IMAGE_BYTES 100000000 CACHE STRING
		"Fail test_windows_image_size when QEGTRAIN.exe SizeOfImage exceeds this many bytes")
	egtrain_add_test(test_windows_image_size
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/pe_image_size.py
			$<TARGET_FILE:QEGTRAIN> --max-bytes ${EGTRAIN_MAX_PE_IMAGE_BYTES}
			--machine x64 --subsystem windows --stack-reserve ${EGTRAIN_STACK_RESERVE_BYTES}
		LABELS integration)
	egtrain_add_test(test_windows_default_stack
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/pe_image_size.py
			$<TARGET_FILE:scene_tool> --max-bytes ${EGTRAIN_MAX_PE_IMAGE_BYTES}
			--machine x64 --subsystem console --stack-reserve 1048576
		LABELS integration)
endif()
egtrain_add_test(test_pe_image_size
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_pe_image_size.py
	LABELS unit)
egtrain_add_test(test_pe_imports
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_pe_imports.py
	LABELS unit)
# The signature check runs on copies of scene_tool.exe that a throwaway self-signed certificate signs.
if(WIN32)
	find_program(EGTRAIN_POWERSHELL NAMES pwsh powershell)
	if(EGTRAIN_POWERSHELL)
		egtrain_add_test(test_verify_signatures
			COMMAND ${EGTRAIN_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
				-File ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_verify_signatures.ps1
				-SceneTool $<TARGET_FILE:scene_tool>
			LABELS integration
			TIMEOUT 300)
	else()
		message(STATUS "Windows: skipping test_verify_signatures (no PowerShell found)")
	endif()
endif()
egtrain_add_test(test_package_start_smoke
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_package_start_smoke.py
	LABELS unit)
egtrain_add_test(test_linux_rehearsal
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_linux_rehearsal.py
	LABELS unit)
# A 32-bit configure stops with the message that names the supported platform. A Ninja build tree checks this with
# the default generator, which is Visual Studio on Windows.
if(WIN32)
	if(CMAKE_GENERATOR MATCHES "Visual Studio")
		set(egtrain_win32_generator -G ${CMAKE_GENERATOR})
	else()
		set(egtrain_win32_generator)
	endif()
	egtrain_add_test(test_win32_configure_rejected
		COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}
			-B ${CMAKE_CURRENT_BINARY_DIR}/win32-configure-check ${egtrain_win32_generator} -A Win32
		LABELS integration
		PASS_REGULAR_EXPRESSION "EGTRAIN supports 64-bit Windows"
		TIMEOUT 300)
endif()
