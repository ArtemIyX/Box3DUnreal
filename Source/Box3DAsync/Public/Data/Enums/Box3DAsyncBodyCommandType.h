#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncBodyCommandType.generated.h"

/** Operations submitted to the async worker at the next fixed-step boundary. */
UENUM(BlueprintType)
enum class EBox3DAsyncBodyCommandType : uint8
{
	SetLinearVelocity,
	AddLinearVelocity,
	SetAngularVelocity,
	AddAngularVelocity,
	ApplyImpulse,
	ApplyImpulseAtPoint,
	ApplyForce,
	ApplyForceAtPoint,
	ApplyTorque,
	ApplyAngularImpulse,
	SetAwake,
	SetEnabled,
	SetKinematicTarget
};
