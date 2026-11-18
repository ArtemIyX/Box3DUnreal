#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DFragmentFace.h"
#include "Data/Structs/Box3DFragmentNeighbor.h"

namespace Box3D::Fracture
{
	/// One convex fragment produced by Fracture(). All quantities in UE cm.
	struct FBox3DFragmentData
	{
		TArray<FVector> Vertices;
		TArray<FBox3DFragmentFace> Faces;
		double Volume = 0.0;
		FVector Centroid = FVector::ZeroVector;

		/// Symmetric adjacency: A lists B iff B lists A, with matching areas.
		/// Derived from Faces; sorted by FragmentIndex ascending.
		TArray<FBox3DFragmentNeighbor> Neighbors;
	};
}
