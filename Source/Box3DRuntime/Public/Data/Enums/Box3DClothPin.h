#pragma once

#include "CoreMinimal.h"
#include "Box3DClothPin.generated.h"

/// Which cloth particles are pinned to the anchor.
UENUM(BlueprintType)
enum class EBox3DClothPin : uint8
{
	/// The whole top row — a curtain or hanging banner.
	TopEdge,
	/// Only the two top corners — a flag or draped sheet.
	TopCorners,
	/// Nothing pinned — a loose sheet that falls and drapes over the world.
	None,
};
