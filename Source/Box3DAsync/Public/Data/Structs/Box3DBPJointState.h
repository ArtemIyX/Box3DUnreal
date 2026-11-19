#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPJointState.generated.h"

/** Joint force is in N, torque in N*m, sampled after a worker step. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPJointState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity Joint;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 CaptureSerial = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 SimulationStep = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector ConstraintForce = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector ConstraintTorque = FVector::ZeroVector;
};
