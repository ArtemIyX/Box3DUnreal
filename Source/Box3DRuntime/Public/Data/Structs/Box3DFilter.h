#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Data/Enums/Box3DChannel.h"
#include "Box3DFilter.generated.h"

class AActor;
class UBox3DBodyComponent;

/// Shape collision filter, a thin UE face over b3Filter.
USTRUCT(BlueprintType)
struct BOX3DRUNTIME_API FBox3DFilter
{
	GENERATED_BODY()

	/// What this shape is (its categories).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D",
		meta = (Bitmask, BitmaskEnum = "/Script/Box3DRuntime.EBox3DChannel"))
	int32 CategoryBits = 1 << static_cast<int32>(EBox3DChannel::WorldDynamic);

	/// Which categories this shape collides with. Default: everything.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D",
		meta = (Bitmask, BitmaskEnum = "/Script/Box3DRuntime.EBox3DChannel"))
	int32 MaskBits = -1;

	/// Non-zero group overrides the mask: same positive group always collides, same
	/// negative group never collides (e.g. give a ragdoll a unique negative group).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	int32 GroupIndex = 0;
};
