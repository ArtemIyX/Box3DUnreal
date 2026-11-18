#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncFieldMode.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncFieldMode : uint8
{
	Acceleration,
	Wind
};
