#pragma once

#include "CoreMinimal.h"
#include "Box3DAsyncBodyMotion.generated.h"

/** Motion type for an actorless async body. */
UENUM(BlueprintType)
enum class EBox3DAsyncBodyMotion : uint8 { Static, Dynamic, Kinematic };
