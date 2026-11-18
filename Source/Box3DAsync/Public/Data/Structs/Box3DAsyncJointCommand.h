#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncJointCommandType.h"
#include "Box3DAsyncJointCommand.generated.h"

/** Linear lengths and velocities are cm and cm/s; angular values are radians and rad/s. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DAsyncJointCommand
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DAsyncJointCommandType Type = EBox3DAsyncJointCommandType::SetMotorSpeed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Scalar = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector Vector = FVector::ZeroVector;
};
