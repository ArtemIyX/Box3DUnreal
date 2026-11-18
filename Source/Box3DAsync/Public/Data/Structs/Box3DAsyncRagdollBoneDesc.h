#pragma once

#include "CoreMinimal.h"
#include "World/Box3DAsyncSubsystem.h"

/** One simulated bone. Body must be dynamic and use unit world scale. */
struct BOX3DASYNC_API FBox3DAsyncRagdollBoneDesc
{
	FName BoneName;
	FBox3DAsyncBodyDesc Body;
};
