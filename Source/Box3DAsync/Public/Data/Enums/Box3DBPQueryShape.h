#pragma once

#include "CoreMinimal.h"
#include "Box3DBPQueryShape.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPQueryShape : uint8
{
	Sphere,
	Capsule,
	Box,
	Convex
};
