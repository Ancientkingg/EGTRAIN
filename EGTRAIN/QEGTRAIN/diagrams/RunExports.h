#ifndef RUNEXPORTS_H
#define RUNEXPORTS_H

// The CSV text of a finished run is in RunCsv.h, what its diagrams show in BlockingTimeData.h and RouteDiagramData.h,
// and fullTableImage in ResultFrame.h. This header includes all of them for the files that do not include those headers
// yet.

#include "diagrams/BlockingTimeData.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RouteDiagramData.h"
#include "diagrams/RunCsv.h"

#include <QImage>

#endif
