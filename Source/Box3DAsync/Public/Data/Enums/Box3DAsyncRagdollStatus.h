#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncRagdollStatus.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncRagdollStatus : uint8
{
	Unavailable,
	CreatingBodies,
	CreatingJoints,
	Active,
	Removing,
	Removed,
	Failed
};
