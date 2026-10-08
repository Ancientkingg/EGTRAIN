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
if(EGTRAIN_OPENMP_INCLUDE)
	target_include_directories(test_characterization PRIVATE ${EGTRAIN_OPENMP_INCLUDE})
endif()
if(EGTRAIN_OPENMP_COMPILE_OPTIONS)
	target_compile_options(test_characterization PRIVATE ${EGTRAIN_OPENMP_COMPILE_OPTIONS})
endif()
if(EGTRAIN_OPENMP_LINK_LIBRARIES)
	target_link_libraries(test_characterization PRIVATE ${EGTRAIN_OPENMP_LINK_LIBRARIES})
elseif(OpenMP_CXX_FOUND)
	target_link_libraries(test_characterization PRIVATE OpenMP::OpenMP_CXX)
endif()

set(EGTRAIN_CHARACTERIZATION_FIXTURE ${SRC_DIR}/tests/fixtures/scenes/line)
set(EGTRAIN_CHARACTERIZATION_EXPECTED ${CMAKE_CURRENT_SOURCE_DIR}/expected)

# Cases that run in CTest. Every other case in the table of
# test_characterization.cpp can be run by hand with --case.
set(EGTRAIN_CHARACTERIZATION_CASES
	single-train)

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

# One OpenMP thread: the braking-point search in RollingStock.h writes shared
# variables outside its critical section, so the thread count must not vary.
set_tests_properties(${EGTRAIN_CHARACTERIZATION_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 60
	ENVIRONMENT "OMP_NUM_THREADS=1;QT_QPA_PLATFORM=offscreen")
set_tests_properties(${EGTRAIN_CHARACTERIZATION_SCENE_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 300
	ENVIRONMENT "OMP_NUM_THREADS=1;QT_QPA_PLATFORM=offscreen")
set_property(TEST characterization_repeat_netherlands APPEND PROPERTY LABELS slow)
