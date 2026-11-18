#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncFieldMode.h"

/** Copied spherical field. Radius is cm; zero selects the whole physics world.
 * Acceleration mode uses cm/s^2. Wind mode targets cm/s with drag in 1/s.
 * The worker applies the field once per real fixed step to dynamic bodies. */
struct BOX3DASYNC_API FBox3DAsyncFieldDesc
{
	EBox3DAsyncFieldMode Mode = EBox3DAsyncFieldMode::Wind;
	FVector Center = FVector::ZeroVector;
	float Radius = 0.0f;
	FVector Vector = FVector::ZeroVector;
	float WindDrag = 1.0f;
	float FalloffExponent = 0.0f;
	bool bWakeBodies = true;
};
