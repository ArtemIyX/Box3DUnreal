#pragma once

#include "CoreMinimal.h"
#include "Box3DBakedShapeKind.generated.h"

/// One shape's kind. Mirrors what the bake extraction produces; the loader
/// switches on it.
UENUM()
enum class EBox3DBakedShapeKind : uint8
{
	/// Convex hull rebuilt at load from Points (b3CreateHull is deterministic).
	Hull,
	/// Static tri-mesh: Points = vertices, Indices = triangles. Winding is
	/// already flipped to box3d's convention at bake time.
	Mesh,
	/// CenterA + Radius.
	Sphere,
	/// CenterA / CenterB (hemisphere centers) + Radius.
	Capsule,
};
