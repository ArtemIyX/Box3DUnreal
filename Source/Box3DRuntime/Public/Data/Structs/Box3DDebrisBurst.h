#pragma once

#include "CoreMinimal.h"

/// Per-fragment burst arrays for the Debris tier, in the fractured actor's
/// space (actor transform maps them to world). The Niagara hookup consuming
/// these is a later D3 slice; until then they are inspectable data.
struct FBox3DDebrisBurst
{
	/// Fragment centroids.
	TArray<FVector> Positions;

	/// Radial scatter velocities (cm/s): away from the impact point at the
	/// configured debris speed.
	TArray<FVector> Velocities;

	/// Equivalent cube edge lengths (cm): cbrt(fragment volume).
	TArray<float> Sizes;
};
