#pragma once

#include "CoreMinimal.h"

namespace Box3D::Fracture
{
	/// The convex volume to fracture: a closed convex polytope with faces wound
	/// counter-clockwise seen from outside.
	struct FFractureProxy
	{
		TArray<FVector> Vertices;
		TArray<TArray<int32>> Faces;
	};
}
