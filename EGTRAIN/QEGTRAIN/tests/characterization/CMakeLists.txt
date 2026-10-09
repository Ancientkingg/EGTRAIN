# Behaviour characterization of the simulation core. One executable drives the
# real DispatchController in-process; each case is its own CTest entry.
add_executable(test_characterization
	${SRC_DIR}/tests/characterization/test_characterization.cpp
	${SRC_DIR}/app/DispatchController.cpp
	${SRC_DIR}/diagrams/RunResults.cpp
	${SRC_DIR}/io/RailMLParser.cpp
	${SRC_DIR}/io/third_party/pugixml.cpp
	${SRC_DIR}/scene/SceneDiagnostic.cpp
	${SRC_DIR}/scene/SceneModel.cpp
	${SRC_DIR}/scene/SceneValidator.cpp
	${SRC_DIR}/scene/SectionInventory.cpp
	${SRC_DIR}/simulation/Infrastructure.cpp
	${SRC_DIR}/simulation/InitialParameters.cpp
	${SRC_DIR}/simulation/NumberGenerator.cpp
	${SRC_DIR}/simulation/Optimisation.cpp
	${SRC_DIR}/simulation/Passengers.cpp
	${SRC_DIR}/simulation/RollingStock.cpp
	${SRC_DIR}/simulation/Signalling.cpp
	${SRC_DIR}/simulation/Simulation.cpp
	${SRC_DIR}/simulation/SimulationWorker.cpp
	${SRC_DIR}/util/Logger.cpp
	${SRC_DIR}/util/PlaybackProfiler.cpp
	${SRC_DIR}/util/TrajectoryUtil.cpp)
target_include_directories(test_characterization PRIVATE ${SRC_DIR})
target_link_libraries(test_characterization PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts cppzmq nlohmann_json::nlohmann_json)

set(EGTRAIN_CHARACTERIZATION_FIXTURE ${SRC_DIR}/tests/fixtures/scenes/line)
set(EGTRAIN_CHARACTERIZATION_EXPECTED ${CMAKE_CURRENT_SOURCE_DIR}/expected)

# Cases that run in CTest, in the order of the table in
# test_characterization.cpp. Each has a golden file in expected/.
set(EGTRAIN_CHARACTERIZATION_CASES
	single-train
	follow-level-none
	follow-level-0
	follow-level-1
	follow-level-2
	follow-level-3
	follow-level-4
	follow-level-5
	sf-forward-level-none
	sf-forward-level-0
	sf-forward-level-1
	sf-forward-level-2
	sf-forward-level-3
	sf-forward-level-4
	sf-forward-level-5
	sf-reverse-level-none
	sf-reverse-level-0
	sf-reverse-level-1
	sf-reverse-level-2
	sf-reverse-level-3
	sf-reverse-level-4
	sf-reverse-level-5
	sf-adjacent-level-none
	sf-adjacent-level-0
	sf-adjacent-level-1
	sf-adjacent-level-2
	sf-staggered-level-none
	sf-staggered-level-0
	sf-staggered-level-1
	sf-staggered-level-2
	sf-last-level-none
	sf-last-level-0
	sf-last-level-1
	sf-last-level-2
	sf-first-level-none
	sf-first-level-0
	sf-first-level-1
	sf-first-level-2
	late-leader-level-3
	late-leader-level-4)

foreach(case IN LISTS EGTRAIN_CHARACTERIZATION_CASES)
	add_test(NAME characterization_${case}
		COMMAND test_characterization
			--fixture ${EGTRAIN_CHARACTERIZATION_FIXTURE}
			--expect ${EGTRAIN_CHARACTERIZATION_EXPECTED}
			--case ${case}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
	list(APPEND EGTRAIN_CHARACTERIZATION_TESTS characterization_${case})
endforeach()

# The same scene sequence runs in one process, the first case again at the end,
# and both runs of it must be identical.
add_test(NAME characterization_repeatable
	COMMAND test_characterization --repeat
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#follow-level-3"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-forward-level-0"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#single-train"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-reverse-level-3"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#late-leader-level-4"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#follow-level-3"
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND EGTRAIN_CHARACTERIZATION_TESTS characterization_repeatable)

# Committed scenes run back to back in one process. A scene that runs again,
# directly or after another scene, must give the same result as its first run.
set(EGTRAIN_COMMITTED_SCENES ${SRC_DIR}/Scenes)
add_test(NAME characterization_repeat_paimpol
	COMMAND test_characterization --repeat
		${EGTRAIN_COMMITTED_SCENES}/Paimpol
		${EGTRAIN_COMMITTED_SCENES}/Paimpol
		${EGTRAIN_COMMITTED_SCENES}/Assignment_Gvc_Gdg_Ut
		${EGTRAIN_COMMITTED_SCENES}/Paimpol
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME characterization_repeat_lebanon_milano
	COMMAND test_characterization --repeat
		${EGTRAIN_COMMITTED_SCENES}/Lebanon
		${EGTRAIN_COMMITTED_SCENES}/Milano_Brescia
		${EGTRAIN_COMMITTED_SCENES}/Lebanon
		${EGTRAIN_COMMITTED_SCENES}/Milano_Brescia
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME characterization_repeat_netherlands
	COMMAND test_characterization --repeat
		${EGTRAIN_COMMITTED_SCENES}/Netherlands
		${EGTRAIN_COMMITTED_SCENES}/Lebanon
		${EGTRAIN_COMMITTED_SCENES}/Netherlands
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(EGTRAIN_CHARACTERIZATION_SCENE_TESTS
	characterization_repeat_paimpol
	characterization_repeat_lebanon_milano
	characterization_repeat_netherlands)

set_tests_properties(${EGTRAIN_CHARACTERIZATION_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 60
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(${EGTRAIN_CHARACTERIZATION_SCENE_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 300
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(TEST characterization_repeat_netherlands APPEND PROPERTY LABELS slow)
