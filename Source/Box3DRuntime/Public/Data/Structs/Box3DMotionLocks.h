#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DMotionLocks.generated.h"

class UPhysicalMaterial;
class UBox3DBodyComponent;

/// Axes on which the body's motion is locked (world axes).
USTRUCT(BlueprintType)
struct FBox3DMotionLocks
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	bool bLinearX = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	bool bLinearY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	bool bLinearZ = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	bool bAngularX = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	bool bAngularY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D")
	bool bAngularZ = false;
};
