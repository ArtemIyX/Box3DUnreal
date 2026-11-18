#pragma once

#include "CoreMinimal.h"
#include "Box3DBPQueryFailure.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPQueryFailure : uint8
{
	None,
	IdentityUnavailable,
	WorkLimit,
	WorkerUnavailable
};
