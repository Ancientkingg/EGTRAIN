#include "scene/SceneModel.h"
#include "simulation/Signalling.h"
#include <iostream>

Logger owl;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static Section makeSection(const std::string& id, double startX, double endX) {
	Section section;
	section.ID = id;
	section.start_node.X = startX;
	section.end_node.X = endX;
	section.length = endX - startX;
	section.total_nodes = 2;
	section.total_arcs = 1;
	section.arcs_in_signalling_block_section[0].startNode = section.start_node;
	section.arcs_in_signalling_block_section[0].endNode = section.end_node;
	section.arcs_in_signalling_block_section[0].length = section.length;
	return section;
}

static void resetRouteState() {
	Blocks = 0;
	N_Routes = 0;
	train_route.clear();
}

static void seedThreeSections() {
	signalling_block_sections[0] = makeSection("A", 0.0, 1.0);
	signalling_block_sections[1] = makeSection("B", 1.0, 2.0);
	signalling_block_sections[2] = makeSection("C", 2.0, 3.0);
	Blocks = 3;
}

int main() {
	bool ok = true;

	resetRouteState();
	seedThreeSections();

	SceneModel scene;
	SceneRoute route;
	route.id = "route.alpha";
	route.blocks = {"A", "B", "C"};
	scene.routes.push_back(route);

	setUpRoutesFromScene(scene);

	ok &= expect(N_Routes == 1, "scene route count becomes N_Routes");
	ok &= expect(train_route.size() == 1, "train_route has one native route");
	if (!train_route.empty()) {
		const Route& built = train_route[0];
		ok &= expect(built.N_Block_Sections == 3, "native route has three sections");
		ok &= expect(built.sequence_of_block_sections[0].ID == "A", "first section is A");
		ok &= expect(built.sequence_of_block_sections[1].ID == "B", "second section is B");
		ok &= expect(built.sequence_of_block_sections[2].ID == "C", "third section is C");
		ok &= expect(built.ID == "A->C", "native route id matches legacy derived id");
		ok &= expect(!built.reversed_direction, "native route keeps forward direction");
		ok &= expect(built.x_of_start_node == 0.0, "native route start node set");
		ok &= expect(built.x_of_end_node == 3.0, "native route end node set");
	}

	resetRouteState();
	signalling_block_sections[0] = makeSection("@5-B6@", 0.0, 1.0);
	signalling_block_sections[1] = makeSection("@5-B6@-branch", 1.0, 2.0);
	Blocks = 2;
	setDependenciesBetweenBlocks();
	ok &= expect(signalling_block_sections[0].N_ConnectedBS == 2,
		"Copenhagen dependency is added once");
	int copenhagenDependencies = 0;
	for (int i = 0; i < signalling_block_sections[0].N_ConnectedBS; i++) {
		if (signalling_block_sections[0].IDConnectedBS[i] == "@1-B30@-4.592000/@5-B7@-4.620000")
			copenhagenDependencies++;
	}
	ok &= expect(copenhagenDependencies == 1, "Copenhagen dependency has the expected ID");

	resetRouteState();
	seedThreeSections();

	SceneModel reversedScene;
	SceneRoute reversedRoute;
	reversedRoute.id = "route.reversed";
	reversedRoute.blocks = {"C", "B", "A"};
	reversedScene.routes.push_back(reversedRoute);

	setUpRoutesFromScene(reversedScene);

	ok &= expect(N_Routes == 1, "reversed scene route count becomes N_Routes");
	ok &= expect(train_route.size() == 1, "train_route has one reversed native route");
	if (!train_route.empty()) {
		const Route& built = train_route[0];
		ok &= expect(built.N_Block_Sections == 3, "reversed native route has three sections");
		ok &= expect(built.reversed_direction, "native route marks reversed direction");
		ok &= expect(built.sequence_of_block_sections[0].ID == "C", "reversed route first section is C");
		ok &= expect(built.sequence_of_block_sections[2].ID == "A", "reversed route third section is A");
		ok &= expect(built.ID == "C->A", "reversed native route id matches legacy derived id");
	}

	resetRouteState();
	return ok ? 0 : 1;
}
