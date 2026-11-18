#pragma once

#include "CoreMinimal.h"

namespace Box3D::Deform
{
	/// One circular dent stamp in the same space as Vertices. ImpactNormal points
	/// out of the struck surface; vertices move opposite it. FalloffExponent 0
	/// produces a flat stamp, 1 is linear, and larger values tighten the centre.
	struct FBox3DVertexDent
	{
		FVector ImpactPoint = FVector::ZeroVector;
		FVector ImpactNormal = FVector::UpVector;
		double RadiusCm = 10.0;
		double FalloffExponent = 2.0;
		double MaxDepthCm = 2.0;
	};
}
