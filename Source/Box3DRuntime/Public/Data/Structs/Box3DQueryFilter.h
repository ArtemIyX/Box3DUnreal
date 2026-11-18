#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Data/Enums/Box3DChannel.h"
#include "Box3DQueryFilter.generated.h"

class AActor;
class UBox3DBodyComponent;

/// Query filter, a thin UE face over b3QueryFilter.
USTRUCT(BlueprintType)
struct BOX3DRUNTIME_API FBox3DQueryFilter
{
	GENERATED_BODY()

	/// What the query itself is. Default: everything.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D",
		meta = (Bitmask, BitmaskEnum = "/Script/Box3DRuntime.EBox3DChannel"))
	int32 CategoryBits = -1;

	/// Which shape categories the query accepts. Default: everything.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D",
		meta = (Bitmask, BitmaskEnum = "/Script/Box3DRuntime.EBox3DChannel"))
	int32 MaskBits = -1;
};
