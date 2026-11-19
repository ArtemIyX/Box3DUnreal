#pragma once

#include "CoreMinimal.h"
#include "Box3DReplicationPolicy.generated.h"

UENUM(BlueprintType)
enum class EBox3DReplicationPolicy : uint8
{
	LocalOnly,
	Shared
};

