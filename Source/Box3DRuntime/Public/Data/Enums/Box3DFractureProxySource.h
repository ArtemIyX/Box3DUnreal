#pragma once

#include "CoreMinimal.h"

/// Which source ResolveFractureProxy built the convex fracture proxy from, in
/// preference order. Packaged builds must hit AuthoredConvex or SimpleCollision:
/// RenderVertices reads LOD0 CPU vertex data, which cooking normally strips, so
/// meshes meant to fracture in shipping need authored simple collision.
enum class EBox3DFractureProxySource : uint8
{
	/// No usable geometry — fracture is not possible for this component.
	None,
	/// Convex hull of the body setup's authored convex elements (AggGeom).
	AuthoredConvex,
	/// Convex hull of the simple-collision primitives (box/sphere/capsule elems).
	SimpleCollision,
	/// Convex hull of LOD0 render vertices — editor-only fallback, logs a warning.
	RenderVertices,
};
