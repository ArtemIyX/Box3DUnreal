#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncJointCommandType.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncJointCommandType : uint8
{
	SetLength,
	SetMotorSpeed,
	SetLinearMotorVelocity,
	SetAngularMotorVelocity,
	SetSteeringAngle
};
