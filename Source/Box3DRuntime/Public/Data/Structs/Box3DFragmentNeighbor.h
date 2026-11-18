#pragma once

#include "CoreMinimal.h"

namespace Box3D::Fracture
{
	/// A fragment's shared-face link to one neighboring fragment.
	struct FBox3DFragmentNeighbor
	{
		int32 FragmentIndex = INDEX_NONE;

		/// Area (cm^2) of the single convex face the two fragments share. Later
		/// milestones scale weld break forces by this. Canonical per pair: both
		/// fragments record the area computed on the lower-indexed side, because
		/// quantized bisector offsets mean the two cells' own face polygons can
		/// differ by sub-QuantizeStep slivers (their Face.Area values agree only
		/// to that tolerance).
		double SharedFaceArea = 0.0;
	};
}
