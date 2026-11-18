#pragma once

#include "CoreMinimal.h"
#include "Box3DBPSample.generated.h"

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPSample
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 Step = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	double ScheduledMonotonicSeconds = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 ClockEpoch = 0;
};
