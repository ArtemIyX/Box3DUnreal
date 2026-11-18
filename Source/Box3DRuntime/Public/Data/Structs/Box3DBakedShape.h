#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DBakedShapeKind.h"
#include "Box3DBakedShape.generated.h"

/// One box3d shape in its body's local space. All coordinates are UE centimeters
/// with the owning component's scale already baked in (box3d shapes carry no
/// scale); the loader converts through the ordinary Box3D conversion seam.
USTRUCT()
struct FBox3DBakedShape
{
	GENERATED_BODY()

	UPROPERTY()
	EBox3DBakedShapeKind Kind = EBox3DBakedShapeKind::Hull;

	/// Hull point cloud, or Mesh vertices. Empty for sphere/capsule.
	UPROPERTY()
	TArray<FVector3f> Points;

	/// Mesh triangle indices (3 per triangle, box3d winding). Empty unless
	/// Kind == Mesh.
	UPROPERTY()
	TArray<int32> Indices;

	/// Sphere center / capsule first hemisphere center (cm).
	UPROPERTY()
	FVector3f CenterA = FVector3f::ZeroVector;

	/// Capsule second hemisphere center (cm, unused otherwise).
	UPROPERTY()
	FVector3f CenterB = FVector3f::ZeroVector;

	/// Sphere / capsule radius (cm).
	UPROPERTY()
	float Radius = 0.0f;
};
