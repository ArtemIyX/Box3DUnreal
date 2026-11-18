#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPBodyState.generated.h"

/** A completed worker sample, not a synchronous view of the native world. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPBodyState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FBox3DBPIdentity Body;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 CaptureSerial = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	int64 SimulationStep = 0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	double CaptureMonotonicSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FTransform Transform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector LinearVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	FVector AngularVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	float Mass = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	bool bAwake = false;
	UPROPERTY(BlueprintReadOnly, Category="Box3D")
	bool bEnabled = false;
};
