#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DStructure.h"

namespace Box3D::Structure
{
	struct FBox3DStressSolveStats
	{
		int32 Iterations = 0;
		int32 SolveNodeCount = 0;
		int32 SolveBondCount = 0;
		double ResidualForce = 0.0;
		double ResidualMoment = 0.0;
		bool bCoarsened = false;
	};
}
