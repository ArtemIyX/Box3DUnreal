#pragma once

#include "CoreMinimal.h"
#include "Box3DBPEventStatus.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPEventStatus : uint8
{
	Pending,
	Baseline,
	Events,
	NeedsResync,
	Unavailable,
	Closed
};
