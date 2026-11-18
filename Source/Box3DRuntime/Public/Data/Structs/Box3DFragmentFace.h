#pragma once

#include "CoreMinimal.h"

namespace Box3D::Fracture
{
	/// One convex face of a fragment. Vertex indices wind counter-clockwise seen
	/// from outside the fragment.
	struct FBox3DFragmentFace
	{
		TArray<int32> VertexIndices;

		/// Fragment index sharing this face, or INDEX_NONE for exterior faces
		/// inherited from the proxy surface. Later milestones use this to assign
		/// source vs interior materials.
		int32 NeighborIndex = INDEX_NONE;

		/// Face area (cm^2).
		double Area = 0.0;
	};
}
