#ifndef INFRASTRUCTUREGUIDANCE_H
#define INFRASTRUCTUREGUIDANCE_H

#include <QString>

// The sentence that the infrastructure panel shows above its table. `facet` is the key of a facet (see
// InfrastructurePanel::facetKeys()), `empty` tells that the table lists no entity, and `loaded` that a scene is open.
// Without a scene every facet asks for a case. An empty table says what the facet needs and what to do next. A table
// with entities has a sentence only where the facet has something to explain, which are blocks and routes. The text
// is empty when there is nothing to say and for a key that is no facet.
QString infrastructureGuidanceText(const QString& facet, bool empty, bool loaded);

#endif // INFRASTRUCTUREGUIDANCE_H
