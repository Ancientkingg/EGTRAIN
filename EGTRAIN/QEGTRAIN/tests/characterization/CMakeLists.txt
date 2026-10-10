# Behaviour characterization of the simulation core. One executable drives the
# real DispatchController in-process; each case is its own CTest entry.
add_executable(test_characterization ${SRC_DIR}/tests/characterization/test_characterization.cpp)
target_link_libraries(test_characterization PRIVATE egtrain_dispatch egtrain_diagrams egtrain_railml egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)

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
	sf-entered-level-none
	same-entry-level-none
	same-entry-level-0
	same-entry-level-1
	same-entry-level-2
	same-entry-level-3
	same-entry-level-4
	same-entry-level-5
	entry-order-level-0
	entry-order-level-1
	entry-order-level-2
	sf-late-level-none
	sf-late-level-0
	sf-late-level-3
	sf-retarget-level-0
	late-leader-level-3
	late-leader-level-4
	single-track-level-none
	single-track-level-0
	single-track-level-1
	single-track-level-2
	single-track-level-3
	single-track-level-4
	single-track-level-5
	single-track-follow-level-3
	single-track-follow-level-4
	stub-departure-first-level-0
	stub-arrival-first-level-0
	stub-tie-level-0
	stub-tie-level-1
	stub-tie-level-2
	stub-tie-level-3
	stub-tie-level-4
	stub-tie-level-5
	border-0-2-fwd
	border-0-2-rev
	border-2-0-fwd
	border-2-0-rev
	border-0-3-fwd
	border-0-3-rev
	border-0-1-fwd
	border-0-2-near-fwd
	border-0-1-near-fwd
	pax-board-gui0
	pax-board-gui1
	pax-full-gui0
	pax-full-gui1
	pax-transfer-gui0
	pax-transfer-gui1)

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
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#single-track-level-1"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-forward-level-0"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#single-train"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-reverse-level-3"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#late-leader-level-4"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#single-track-level-1"
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

# The same kind of sequences, each run writing its files into a folder of its own.
# Every distinct step then runs once more in a fresh process, and every file of
# every run must be byte for byte equal to the file of that process.
add_test(NAME characterization_repeat_files_paimpol
	COMMAND test_characterization --repeat-files
		${EGTRAIN_COMMITTED_SCENES}/Paimpol
		${EGTRAIN_COMMITTED_SCENES}/Paimpol
		${EGTRAIN_COMMITTED_SCENES}/Assignment_Gvc_Gdg_Ut
		${EGTRAIN_COMMITTED_SCENES}/Paimpol
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME characterization_repeat_files_lebanon_milano
	COMMAND test_characterization --repeat-files
		${EGTRAIN_COMMITTED_SCENES}/Lebanon
		${EGTRAIN_COMMITTED_SCENES}/Milano_Brescia
		${EGTRAIN_COMMITTED_SCENES}/Lebanon
		${EGTRAIN_COMMITTED_SCENES}/Milano_Brescia
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME characterization_repeat_files_fixture
	COMMAND test_characterization --repeat-files
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-forward-level-3"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-forward-level-3"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#single-track-level-4"
		"${EGTRAIN_CHARACTERIZATION_FIXTURE}#sf-forward-level-3"
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(EGTRAIN_CHARACTERIZATION_SCENE_TESTS
	characterization_repeat_paimpol
	characterization_repeat_lebanon_milano
	characterization_repeat_netherlands
	characterization_repeat_files_paimpol
	characterization_repeat_files_lebanon_milano
	characterization_repeat_files_fixture)

set_tests_properties(${EGTRAIN_CHARACTERIZATION_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 60
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(${EGTRAIN_CHARACTERIZATION_SCENE_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 300
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(TEST characterization_repeat_netherlands characterization_repeat_files_paimpol APPEND PROPERTY LABELS slow)
# It is the longest of these tests and starts first in a parallel run.
set_tests_properties(characterization_repeat_netherlands PROPERTIES COST 40)

# Two crossovers that follow each other directly, at the fixed block levels. A
# separate executable: the harness above is tied to the line fixture.
add_executable(test_crossover_chain ${SRC_DIR}/tests/characterization/test_crossover_chain.cpp)
target_link_libraries(test_crossover_chain PRIVATE egtrain_dispatch egtrain_diagrams egtrain_railml egtrain_sim Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Charts)

set(EGTRAIN_CROSSOVER_CHAIN_FIXTURE ${SRC_DIR}/tests/fixtures/scenes/crossovers)
foreach(level 0 1 2 5)
	add_test(NAME characterization_crossover_chain_level_${level}
		COMMAND test_crossover_chain --fixture ${EGTRAIN_CROSSOVER_CHAIN_FIXTURE} --level ${level}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
	list(APPEND EGTRAIN_CROSSOVER_CHAIN_TESTS characterization_crossover_chain_level_${level})
endforeach()

set_tests_properties(${EGTRAIN_CROSSOVER_CHAIN_TESTS} PROPERTIES
	LABELS "characterization;unit"
	TIMEOUT 60
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Aspects of the mixed signalling routines on routes built by hand. No Qt: only the simulation library.
add_executable(test_signalling_aspects ${SRC_DIR}/tests/characterization/test_signalling_aspects.cpp)
target_link_libraries(test_signalling_aspects PRIVATE egtrain_sim)
add_test(NAME characterization_signalling_aspects
	COMMAND test_signalling_aspects --expect ${EGTRAIN_CHARACTERIZATION_EXPECTED}/signalling-aspects.txt
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(characterization_signalling_aspects PROPERTIES LABELS "characterization;unit" TIMEOUT 60)
