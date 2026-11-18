#pragma once

#include "CoreMinimal.h"
#include "Box3DBPQueryStatus.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPQueryStatus : uint8
{
	Hit,
	Miss,
	Truncated,
	Cancelled,
	Failed,
	WorldEnded
};
