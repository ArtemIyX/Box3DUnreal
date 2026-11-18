#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DChannel.generated.h"

class AActor;
class UBox3DBodyComponent;

/// Collision channels for Box3D shapes and queries. Values are BIT INDICES into the
/// 64-bit box3d category/mask (editor bitmask widgets treat them as such). The names
/// are conventions only — box3d itself sees raw bits.
UENUM(BlueprintType, meta = (Bitflags))
enum class EBox3DChannel : uint8
{
	WorldStatic = 0,
	WorldDynamic = 1,
	Pawn = 2,
	Projectile = 3,
	Debris = 4,
	Sensor = 5,
	Custom6 = 6,
	Custom7 = 7,
	Custom8 = 8,
	Custom9 = 9,
};
