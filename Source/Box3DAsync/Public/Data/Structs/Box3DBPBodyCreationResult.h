#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPBodyCreationResult.generated.h"

/** Worker creation acknowledgment. Shape IDs and keys share the same order. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPBodyCreationResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	bool bSucceeded = false;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity Body;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	TArray<FBox3DBPIdentity> Shapes;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	TArray<int64> SubshapeKeys;
};
