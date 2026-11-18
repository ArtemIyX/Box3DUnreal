#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPEventFilter.generated.h"

USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPEventFilter
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FBox3DBPIdentity Body;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FBox3DBPIdentity Shape;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bContacts = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bSensors = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bImpacts = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bBodyPairs = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MinimumImpactSpeed = 0;
};
