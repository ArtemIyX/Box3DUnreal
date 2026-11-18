#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DShapeType.generated.h"

class UPhysicalMaterial;
class UBox3DBodyComponent;

/// Collision shape attached to the body.
UENUM(BlueprintType)
enum class EBox3DShapeType : uint8
{
	Box,
	Sphere,
	Capsule,
	/// Convex hull cooked from the attached static mesh (authored convex collision
	/// if present, else simplified render geometry).
	ConvexHull,
	/// Exact triangle mesh from the attached static mesh's LOD0 render geometry.
	/// Static bodies only (box3d mesh collision contacts static bodies only);
	/// dynamic/kinematic bodies fall back to ConvexHull with a warning.
	TriangleMesh,
	/// One Box3D shape per element of the attached mesh's authored collision setup
	/// (sphere, capsule, box, and convex elements). Falls back to TriangleMesh
	/// (static) or ConvexHull (other types) when the mesh has no simple collision.
	CollisionAsset,
};
