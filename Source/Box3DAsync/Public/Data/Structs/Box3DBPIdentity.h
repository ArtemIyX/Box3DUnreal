#pragma once

#include "CoreMinimal.h"
#include "Box3DBPIdentity.generated.h"

/** IDs preserve all 64 bits in Blueprint's signed integer representation. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPIdentity
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category="Box3D")
	FGuid World;
	UPROPERTY(BlueprintReadWrite, Category="Box3D")
	int64 Id = 0;
};
