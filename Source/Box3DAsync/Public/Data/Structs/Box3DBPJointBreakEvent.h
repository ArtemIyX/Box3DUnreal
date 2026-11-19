#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPJointBreakEvent.generated.h"

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPJointBreakEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity Joint;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 SimulationStep = 0;
};
