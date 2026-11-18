#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBakedShape.h"
#include "Box3DBakedBody.generated.h"

/// One static body: the source component's (or ISM instance's) world transform
/// with scale baked into the shapes. Self-contained — the runtime instantiates
/// it without the source actor.
USTRUCT()
struct FBox3DBakedBody
{
	GENERATED_BODY()

	/// World transform at bake time. Only location + rotation place the body;
	/// scale is baked into the shape geometry.
	UPROPERTY()
	FTransform WorldTransform = FTransform::Identity;

	/// Source actor/component path within its level — metadata for rebake
	/// diffing, not used at load.
	UPROPERTY()
	FString ActorKey;

	UPROPERTY()
	TArray<FBox3DBakedShape> Shapes;
};
