# Scripted GUI runs. The sources go into QEGTRAIN itself, not into a library: main.cpp is the only
# reference to the runner, and a static library keeps only the members that something references.
target_sources(QEGTRAIN PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/E2ERunner.cpp)
