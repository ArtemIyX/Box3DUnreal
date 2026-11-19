#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncPresentationMode.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncPresentationMode : uint8
{
	AuthoredStaticCollision UMETA(DisplayName = "Authored Static Collision"),
	AuthoredDynamicCollision UMETA(DisplayName = "Authored Dynamic Collision")
};

