#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncBodyCommandType.h"
#include "Box3DBPBodyCommand.generated.h"

/** Values are copied to the physics worker. Forces are applied for one physics step. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPBodyCommand
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DAsyncBodyCommandType Type = EBox3DAsyncBodyCommandType::SetLinearVelocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector Value = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector Point = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FTransform Target = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float TargetDuration = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bWake = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnabled = true;
};
