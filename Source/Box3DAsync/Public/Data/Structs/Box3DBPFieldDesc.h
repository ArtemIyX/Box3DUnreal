#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncFieldMode.h"
#include "Box3DBPFieldDesc.generated.h"

/** Radius zero affects all dynamic bodies. Vector is cm/s for wind, cm/s^2 for acceleration. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPFieldDesc
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DAsyncFieldMode Mode = EBox3DAsyncFieldMode::Wind;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D", meta=(ClampMin="0", Units="cm"))
	float Radius = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector Vector = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D", meta=(ClampMin="0"))
	float WindDrag = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D", meta=(ClampMin="0"))
	float FalloffExponent = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bWakeBodies = true;
};
