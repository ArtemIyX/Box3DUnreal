#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/ActorComponent.h"
#include "Simulation/Box3DTypes.h"
#include "box3d/id.h"

class UBox3DWorldSubsystem;
class UPhysicsAsset;
class UPrimitiveComponent;
class USkeletalMeshComponent;
struct FReferenceSkeleton;

namespace Box3D
{
	struct FRagdollBuildParams
	{
		/// Filter for every ragdoll shape. Give GroupIndex 0 to have a fresh
		/// negative self-collision group allocated per ragdoll.
		FBox3DFilter Filter;

		float LinearDamping = 0.05f;
		float AngularDamping = 1.0f;
		float Friction = 0.7f;

		/// Scales the per-body masses.
		float MassScale = 1.0f;

		/// Clamp every body to at least this fraction of the heaviest body's
		/// mass. Extreme ratios (300 kg torso vs 2 kg hand) make the joint chain
		/// jitter and never sleep. 0 disables the clamp.
		float MinMassFraction = 0.05f;

		/// Map the physics asset's cone/twist limits onto the joints. Off: free
		/// spherical joints (position-only, floppy).
		bool bUseConstraintLimits = true;

		/// Degrees added when widening limits to include the spawn pose (death
		/// animations routinely violate authored limits; snapping back at spawn
		/// spaghettifies heavy ragdolls). Also the effective minimum limit width.
		float LimitSlackDeg = 3.0f;

		/// Joint constraint softness (b3JointDef advanced fields). Hertz > 0
		/// overrides box3d's default (60 Hz, ratio 2). DampingRatio applies only
		/// when Hertz > 0.
		float ConstraintHertz = 0.0f;
		float ConstraintDampingRatio = 2.0f;
	};
}
