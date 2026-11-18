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
	/// One ragdoll body bound to a skeleton bone, with its interpolation segment
	/// (same scheme as the rope/cloth actors: pose at the previous and latest
	/// fixed step, lerped by the subsystem's fixed-step alpha).
	struct FRagdollBone
	{
		int32 BoneIndex = INDEX_NONE;
		b3BodyId BodyId = {};
		FVector P0 = FVector::ZeroVector;
		FVector P1 = FVector::ZeroVector;
		FQuat Q0 = FQuat::Identity;
		FQuat Q1 = FQuat::Identity;
	};
}
