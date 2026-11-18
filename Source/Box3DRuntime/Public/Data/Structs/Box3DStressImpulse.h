#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DStructure.h"

namespace Box3D::Structure
{
	/// One instantaneous impulse applied to a graph node. Units follow the public
	/// UE seam: kg*cm/s at a chunk-local point in centimetres.
	struct FBox3DStressImpulse
	{
		int32 NodeIndex = INDEX_NONE;
		FVector Impulse = FVector::ZeroVector;
		FVector ApplicationPoint = FVector::ZeroVector;
	};
}
