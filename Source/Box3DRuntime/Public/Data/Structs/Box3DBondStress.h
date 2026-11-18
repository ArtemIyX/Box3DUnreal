#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DStructure.h"

namespace Box3D::Structure
{
	/// Resolved load on one fine graph bond. Force is oriented NodeA -> NodeB;
	/// positive axial load is compression and negative axial load is tension.
	struct FBox3DBondStress
	{
		FVector Force = FVector::ZeroVector;
		FVector Moment = FVector::ZeroVector;
		double Compression = 0.0;
		double Tension = 0.0;
		double Shear = 0.0;
		double BendingMoment = 0.0;
		double TorsionMoment = 0.0;
	};
}
