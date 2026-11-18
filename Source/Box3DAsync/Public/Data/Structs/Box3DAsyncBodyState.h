#pragma once

#include "CoreMinimal.h"

/** Copied from one completed worker publication. Velocities use cm/s and rad/s; mass is kg. */
struct BOX3DASYNC_API FBox3DAsyncBodyState
{
	FGuid World;
	uint64 BodyId = 0;
	uint64 CaptureSerial = 0;
	uint64 SimulationStep = 0;
	double CaptureMonotonicSeconds = 0.0;
	FTransform Transform = FTransform::Identity;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	float Mass = 0.0f;
	bool bAwake = false;
	bool bEnabled = false;
};
