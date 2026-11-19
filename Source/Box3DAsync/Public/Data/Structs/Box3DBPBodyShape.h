#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncBodyShapeKind.h"
#include "Box3DBPBodyShape.generated.h"

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPBodyShape
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DAsyncBodyShapeKind Kind = EBox3DAsyncBodyShapeKind::Box;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FTransform LocalTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector HalfExtents = FVector(50.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Radius = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float HalfHeight = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	TArray<FVector> ConvexPoints;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bSensor = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bDetectableBySensors = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bContactEvents = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bImpactEvents = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 CategoryBits = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 MaskBits = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int32 GroupIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Density = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Friction = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Restitution = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 MaterialId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	int64 SubshapeKey = 0;
};
