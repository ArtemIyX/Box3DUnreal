#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DDebrisBurst.h"
#include "Destruction/Box3DFracture.h"
#include "Data/Enums/Box3DFragmentTier.h"
#include "Data/Structs/Box3DTierThresholds.h"
#include "Data/Structs/Box3DEnergyToCellCurve.h"
#include "Data/Structs/Box3DDestructionEventParams.h"
#include "Data/Structs/Box3DDestructionEvent.h"
#include "Data/Structs/Box3DDestructionEventResult.h"

/// D3 damage pipeline data: impact energy intake, the energy -> cell-count
/// mapping curve, and the volume-threshold tier policy that decides which
/// fragments get bodies, which become a Niagara debris burst, and which are
/// dropped outright (docs/DESTRUCTION_PLAN.md § D3). Everything here is pure
/// data + functions — no actors, no effects — so tier decisions and energy
/// mapping are testable without Niagara or a physics world.















namespace Box3D::Destruction
{
	/// Kinetic energy (J) of an impact: 1/2 m v^2 with the approach speed taken
	/// as the relative velocity. Mass in kg, speed in UE cm/s (the unit hit
	/// events report).
	BOX3DRUNTIME_API float ImpactEnergyJoules(float MassKg, float ApproachSpeedCmS);

	/// Map impact energy (J) through the curve. Returns 0 (no fracture) below
	/// MinEnergy, otherwise a cell count in [MinCellCount, MaxCellCount].
	BOX3DRUNTIME_API int32 EnergyToCellCount(float EnergyJoules, const FBox3DEnergyToCellCurve& Curve);

	/// Tier of a single fragment volume (cm^3) under the thresholds.
	BOX3DRUNTIME_API EBox3DFragmentTier ClassifyFragmentTier(double VolumeCm3, const FBox3DTierThresholds& Thresholds);

	/// Classify every fragment; OutTiers is parallel to Fragments.
	BOX3DRUNTIME_API void ClassifyFragmentTiers(const TArray<Fracture::FBox3DFragmentData>& Fragments,
		const FBox3DTierThresholds& Thresholds, TArray<EBox3DFragmentTier>& OutTiers);

	/// Collect the Debris-tier fragments into burst arrays. ImpactPoint and the
	/// fragment data share one space (the fractured actor's); DebrisSpeedCmS
	/// scales the radial scatter velocity. A fragment whose centroid coincides
	/// with the impact point scatters upward.
	BOX3DRUNTIME_API void BuildDebrisBurst(const TArray<Fracture::FBox3DFragmentData>& Fragments,
		const TArray<EBox3DFragmentTier>& Tiers, const FVector& ImpactPoint, float DebrisSpeedCmS,
		FBox3DDebrisBurst& OutBurst);

	/// Convert the replicated tuple into pure fracture inputs. ComponentToWorld
	/// must contain rotation + translation only: proxy scale is already baked.
	BOX3DRUNTIME_API Fracture::FFractureParams MakeFractureParams(
		const FBox3DDestructionEvent& Event, const FTransform& ComponentToWorld);

	/// Pure deterministic event expansion used by authority and visual clients.
	/// It has no actor, renderer, or b3-world dependency.
	BOX3DRUNTIME_API bool GenerateEventFragments(const Fracture::FFractureProxy& Proxy,
		const FTransform& ComponentToWorld, const FBox3DDestructionEvent& Event,
		TArray<Fracture::FBox3DFragmentData>& OutFragments);

	/// Initial D4/D5 bond-health digest for an event's fragment graph. This uses
	/// the same structure graph and stress-solver hash as live structural actors,
	/// but needs no physics world or anchor query.
	BOX3DRUNTIME_API uint32 InitialBondHealthHash(
		const TArray<Fracture::FBox3DFragmentData>& Fragments, float FragmentDensity);
}
