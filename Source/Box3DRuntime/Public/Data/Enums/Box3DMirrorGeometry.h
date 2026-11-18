#pragma once

#include "CoreMinimal.h"
#include "Box3DMirrorGeometry.generated.h"

class UBox3DCollisionData;

/// Geometry the static scene mirror cooks for each mirrored mesh.
UENUM()
enum class EBox3DMirrorGeometry : uint8
{
	/// Exact LOD0 render triangles. Props rest on what the player sees, and one
	/// cooked mesh is shared by reference across every instance of an asset.
	TriangleMesh,
	/// Authored simple collision (sphere/capsule/box/convex elements) when the
	/// mesh has any, TriangleMesh otherwise. Cheaper contacts, coarser geometry.
	PreferSimpleCollision,
};
