#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DBodyType.generated.h"

class UPhysicalMaterial;
class UBox3DBodyComponent;

/// Simulation type, mirrors b3BodyType (values must stay in enum order).
UENUM(BlueprintType)
enum class EBox3DBodyType : uint8
{
	/// Never moves under simulation; may be repositioned manually.
	Static,
	/// Moved by setting its transform/velocity; pushes dynamic bodies, unaffected by them.
	Kinematic,
	/// Fully simulated: gravity, forces, collisions.
	Dynamic,
};
