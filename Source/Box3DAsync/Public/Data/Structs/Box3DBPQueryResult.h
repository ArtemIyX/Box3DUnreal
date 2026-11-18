#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DBPQueryAdmission.h"
#include "Data/Enums/Box3DBPQueryFailure.h"
#include "Data/Enums/Box3DBPQueryKind.h"
#include "Data/Enums/Box3DBPQueryStatus.h"
#include "Data/Structs/Box3DBPHit.h"
#include "Data/Structs/Box3DBPSample.h"
#include "Box3DBPQueryResult.generated.h"

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPQueryResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryStatus Status = EBox3DBPQueryStatus::Failed;
	/** Rejected work has no physics sample and reports its admission reason here. */
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryAdmission Admission = EBox3DBPQueryAdmission::Accepted;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryFailure Failure = EBox3DBPQueryFailure::None;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	EBox3DBPQueryKind Kind = EBox3DBPQueryKind::Line;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 UserTag = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPSample Sample;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	TArray<FBox3DBPHit> Hits;
};
