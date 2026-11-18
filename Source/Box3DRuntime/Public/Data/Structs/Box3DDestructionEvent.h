#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DDestructionEventParams.h"
#include "Box3DDestructionEvent.generated.h"

/// Game-side RPC/GAS payload. MeshId is the stable source-mesh asset path; game
/// code still chooses the target actor/component through its own replicated id.
/// Impact is world-space UE centimetres. The authority sends the event plus the
/// layout hash returned by applying it; receivers validate before accepting
/// predicted fragment state.
USTRUCT(BlueprintType)
struct FBox3DDestructionEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	FName MeshId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	FVector Impact = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	FBox3DDestructionEventParams Params;
};
