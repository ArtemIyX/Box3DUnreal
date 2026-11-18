#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncBodyCommandType.h"

/** Copied command values in Unreal units. Point is a world-space position in centimeters.
 * Forces and torques apply to one fixed step; use repeated commands only when appropriate.
 * Linear quantities use kg, cm and seconds; angular velocities are radians per second. */
struct BOX3DASYNC_API FBox3DAsyncBodyCommand
{
	EBox3DAsyncBodyCommandType Type = EBox3DAsyncBodyCommandType::SetLinearVelocity;
	FVector Value = FVector::ZeroVector;
	FVector Point = FVector::ZeroVector;
	/** Target in world space with unit scale. Duration is at least 0.001 seconds; the resulting
	 * velocity persists until another command changes it. Refresh targets or stop explicitly. */
	FTransform Target = FTransform::Identity;
	float TargetDuration = 0.0f;
	bool bWake = true;
	/** Value for SetAwake or SetEnabled. */
	bool bEnabled = true;
};
