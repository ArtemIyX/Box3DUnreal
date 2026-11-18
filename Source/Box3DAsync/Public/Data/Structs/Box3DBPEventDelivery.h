#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DBPEventStatus.h"
#include "Data/Structs/Box3DBPEvent.h"
#include "Data/Structs/Box3DBPSample.h"
#include "Box3DBPEventDelivery.generated.h"

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPEventDelivery
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPEventStatus Status = EBox3DBPEventStatus::Pending;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 FirstMissedSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 LastMissedSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPSample Sample;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	TArray<FBox3DBPEvent> Events;
};
