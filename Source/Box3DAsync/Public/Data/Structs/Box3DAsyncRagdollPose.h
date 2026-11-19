#pragma once

#include "CoreMinimal.h"

struct BOX3DASYNC_API FBox3DAsyncRagdollPose
{
	FGuid World;
	uint64 RagdollId = 0;
	uint64 CaptureSerial = 0;
	uint64 SimulationStep = 0;
	TArray<FName> BoneNames;
	TArray<FTransform> WorldTransforms;
};
