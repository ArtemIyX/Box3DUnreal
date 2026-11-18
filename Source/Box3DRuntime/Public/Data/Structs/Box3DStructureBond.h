#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DFracture.h"
#include "box3d/id.h"

namespace Box3D::Structure
{
	/// One shared-face link between two chunks. Alive until bBroken.
	struct FBox3DStructureBond
	{
		/// Chunk indices, NodeA < NodeB (canonical pair order).
		int32 NodeA = INDEX_NONE;
		int32 NodeB = INDEX_NONE;

		/// Shared-face area (cm^2) from fracture adjacency — the bond's strength
		/// scale (weld break forces and D5 stress capacity derive from it).
		double Area = 0.0;

		/// Bond midpoint and canonical normal in chunk-local centimetres. Normal
		/// points NodeA -> NodeB; D5 uses it to split axial and shear load.
		FVector Centroid = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;

		/// Remaining health fraction in [0, 1]. D4 events snap it to zero; D5
		/// stress overload erodes it gradually.
		float Health = 1.0f;

		bool bBroken = false;
	};
}
