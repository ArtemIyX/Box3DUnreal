#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DHitResult.generated.h"

class AActor;
class UBox3DBodyComponent;

/// Result of a Box3D ray/shape cast.
USTRUCT(BlueprintType)
struct BOX3DRUNTIME_API FBox3DHitResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	bool bHit = false;

	/// World hit location (cm).
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	FVector Location = FVector::ZeroVector;

	/// Surface normal at the hit.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	FVector Normal = FVector::UpVector;

	/// Fraction along the cast translation [0, 1].
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	float Fraction = 1.0f;

	/// Body component owning the hit shape (null if the body has no component owner).
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	TObjectPtr<UBox3DBodyComponent> Component = nullptr;

	/// Actor owning the hit component.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	TObjectPtr<AActor> Actor = nullptr;

	/// Shape/triangle user material id at the hit point.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int64 UserMaterialId = 0;

	/// Triangle index for mesh/height-field hits, -1 otherwise.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 TriangleIndex = -1;
};
