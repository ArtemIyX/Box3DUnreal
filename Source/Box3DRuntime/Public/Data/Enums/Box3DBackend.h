#pragma once

#include "CoreMinimal.h"
#include "Box3DBackend.generated.h"

/** Independently enabled Box3D world backends. */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EBox3DBackend : uint8
{
	None = 0 UMETA(Hidden),
	Sync = 1 << 0 UMETA(DisplayName = "Sync"),
	Async = 1 << 1 UMETA(DisplayName = "Async"),
};
ENUM_CLASS_FLAGS(EBox3DBackend)
