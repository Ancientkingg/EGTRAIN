# Scripted GUI runs. The sources go into QEGTRAIN itself, not into a library: main.cpp is the only
# reference to the runner, and a static library keeps only the members that something references.
target_sources(QEGTRAIN PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/E2ERunner.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/LegacyImport.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/SceneDrop.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/SceneRender.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/StationOverlay.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/TrackPreview.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/VisualPolish.cpp)

# TrackPreview.cpp and VisualPolish.cpp include ui_MainWindow.h, and AUTOUIC looks for MainWindow.ui next to the source that includes it.
set_property(TARGET QEGTRAIN APPEND PROPERTY AUTOUIC_SEARCH_PATHS ${SRC_DIR}/app)
