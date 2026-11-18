#pragma once

#include "CoreMinimal.h"

/** Forces are newtons, torques N*m; values come from a completed worker step. */
struct BOX3DASYNC_API FBox3DAsyncJointState
{
	FGuid World;
	uint64 JointId = 0;
	uint64 CaptureSerial = 0;
	uint64 SimulationStep = 0;
	FVector ConstraintForce = FVector::ZeroVector;
	FVector ConstraintTorque = FVector::ZeroVector;
};
