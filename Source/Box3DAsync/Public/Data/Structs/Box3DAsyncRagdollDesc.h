#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DAsyncRagdollBoneDesc.h"
#include "Data/Structs/Box3DAsyncRagdollLinkDesc.h"

/** Copied on admission; no worker access to animation or physics asset objects. */
struct BOX3DASYNC_API FBox3DAsyncRagdollDesc
{
	/** Disable collisions among bones in this group. */
	bool bDisableSelfCollision = true;
	TArray<FBox3DAsyncRagdollBoneDesc> Bones;
	TArray<FBox3DAsyncRagdollLinkDesc> Links;
};
