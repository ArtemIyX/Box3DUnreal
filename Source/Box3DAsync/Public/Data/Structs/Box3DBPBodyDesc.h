#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncBodyMotion.h"
#include "Data/Structs/Box3DBPBodyShape.h"
#include "Box3DBPBodyDesc.generated.h"

/** Shape and body transforms must have unit scale. Linear velocity uses cm/s. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPBodyDesc
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DAsyncBodyMotion Motion = EBox3DAsyncBodyMotion::Static;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FTransform Transform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector InitialVelocity = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector InitialAngularVelocity = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float GravityScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float LinearDamping = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float AngularDamping = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MassOverride = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableSleep = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bStartAwake = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bStartEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bBullet = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	TArray<FBox3DBPBodyShape> Shapes;
};
