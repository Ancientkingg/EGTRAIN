#ifndef INFRASTRUCTUREGUIDANCE_H
#define INFRASTRUCTUREGUIDANCE_H

#include <QString>

// What the sentence of the infrastructure panel depends on: whether a scene is open, whether it can be edited (not
// during a run), and how many of the entities exist that other facets build on.
struct InfrastructureGuidanceState {
	bool loaded = false;
	bool editable = false;
	int tracks = 0;
	int nodes = 0;
	int stations = 0;
};

// The sentence that the infrastructure panel shows above its table. `facet` is the key of a facet (see
// InfrastructurePanel::facetKeys()), `empty` tells that the table lists no entity. Without a scene every facet asks
// for a case. While the scene cannot be edited, Add is off and the sentence says so instead of asking to add. An empty
// table says what to do next, and names a prerequisite (a track, two nodes, a station) only while the scene lacks it.
// A table with entities has a sentence only where the facet has something to explain, which are blocks and routes.
// The text is empty when there is nothing to say and for a key that is no facet.
QString infrastructureGuidanceText(const QString& facet, bool empty, const InfrastructureGuidanceState& state);

#endif // INFRASTRUCTUREGUIDANCE_H
