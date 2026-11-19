#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncJointType.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncJointType : uint8
{
	Distance,
	Revolute,
	Prismatic,
	Spherical,
	Weld,
	Motor,
	Wheel
};
