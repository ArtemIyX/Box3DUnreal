#pragma once

#include "CoreMinimal.h"
#include "Box3DFragmentTier.generated.h"

/// Simulation tier of one fracture fragment, decided purely from its volume.
UENUM(BlueprintType)
enum class EBox3DFragmentTier : uint8
{
	/// Full citizen: hull body, welds, and rendered PMC sections.
	Body,
	/// Below PhysicsVolumeThreshold: no body, no welds, no sections — handed to
	/// a Niagara burst (position/velocity/size arrays) instead.
	Debris,
	/// Below RenderVolumeThreshold: does not exist at all.
	Dust,
};
