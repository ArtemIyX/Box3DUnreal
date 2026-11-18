#pragma once

#include "CoreMinimal.h"
#include "Box3DBPEndCause.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPEndCause : uint8
{
	NativeEnd,
	Removed
};
