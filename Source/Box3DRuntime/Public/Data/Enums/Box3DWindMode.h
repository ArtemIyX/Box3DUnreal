#pragma once

#include "CoreMinimal.h"
#include "Box3DWindMode.generated.h"

/// The shape of the wind velocity field.
UENUM(BlueprintType)
enum class EBox3DWindMode : uint8
{
	/// Uniform wind along the actor's forward (X) axis.
	Directional,
	/// Scrolling Perlin field: direction and strength vary over space and
	/// time. Gusty, chaotic — leaves and loose cloth.
	Turbulence,
	/// Wind follows the actor's spline: at any point it blows along the
	/// tangent of the nearest spline location. A wind tunnel on a path.
	Spline,
	/// Swirls around the actor's up axis: tangential flow plus optional
	/// inward pull and updraft. A tornado.
	Vortex,
};
