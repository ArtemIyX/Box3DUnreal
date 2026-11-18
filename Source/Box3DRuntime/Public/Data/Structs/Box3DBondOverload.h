#pragma once

#include "CoreMinimal.h"
#include "Destruction/Box3DStructure.h"

namespace Box3D::Structure
{
	struct FBox3DBondOverload
	{
		double TensionPa = 0.0;
		double CompressionPa = 0.0;
		double ShearPa = 0.0;
		/// Maximum stress/threshold ratio. Damage begins strictly above 1.
		double Ratio = 0.0;
	};
}
