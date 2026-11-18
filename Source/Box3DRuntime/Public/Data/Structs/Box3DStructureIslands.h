#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DFracture.h"
#include "box3d/id.h"

namespace Box3D::Structure
{
	/// Result of one connectivity update.
	struct FBox3DStructureIslands
	{
		/// Chunk groups left with no live path to any anchor, each sorted
		/// ascending; groups ordered by their smallest member. The graph does not
		/// modify island nodes — promotion decides their fate.
		TArray<TArray<int32>> Islands;

		/// Nodes the flood-fill dequeued. The locality bound for tests: an update
		/// next to anchors must not walk the whole graph.
		int32 VisitCount = 0;
	};
}
