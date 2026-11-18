#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncBodyShapeKind.generated.h"

UENUM(BlueprintType)
enum class EBox3DAsyncBodyShapeKind : uint8 { Sphere, Capsule, Box, Convex };
