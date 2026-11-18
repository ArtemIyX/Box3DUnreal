#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DAsyncJointDesc.h"

/** Parent/child refer to indices in the ragdoll bone array. */
struct BOX3DASYNC_API FBox3DAsyncRagdollLinkDesc
{
	int32 Parent = INDEX_NONE;
	int32 Child = INDEX_NONE;
	FBox3DAsyncJointDesc Joint;
};
