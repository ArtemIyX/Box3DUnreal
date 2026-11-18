#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DStructure.h"

namespace Box3D::Structure
{
	/// Material capacities at the UE-facing seam. Force components are converted
	/// from kg*cm/s^2 over cm^2 to pascals before these thresholds are tested.
	/// A non-positive threshold disables damage for that component.
	struct FBox3DStressThresholds
	{
		double TensionPa = 1.0e6;
		double CompressionPa = 5.0e6;
		double ShearPa = 1.0e6;
	};
}
