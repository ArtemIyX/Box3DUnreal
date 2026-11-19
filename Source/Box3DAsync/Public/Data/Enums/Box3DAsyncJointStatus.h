#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncJointStatus.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncJointStatus : uint8
{
	Unavailable,
	Pending,
	Active,
	Failed,
	Broken,
	RemovalQueued
};
