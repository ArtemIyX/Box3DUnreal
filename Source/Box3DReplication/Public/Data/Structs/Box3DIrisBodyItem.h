#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/SoftObjectPath.h"
#include "Box3DIrisBodyItem.generated.h"

USTRUCT()
struct BOX3DREPLICATION_API FBox3DIrisBodyItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 BodyId = 0;
	UPROPERTY()
	FGuid BodyKey;
	UPROPERTY()
	uint32 RelevanceRevision = 0;
	UPROPERTY()
	uint32 DiscontinuityRevision = 0;
	UPROPERTY()
	uint32 OriginRevision = 0;
	UPROPERTY()
	bool bAuthoredVisual = false;
	UPROPERTY()
	FSoftObjectPath VisualAsset;
	UPROPERTY()
	FVector3f VisualScale = FVector3f::OneVector;
	UPROPERTY()
	FVector_NetQuantize Position;
	UPROPERTY()
	FQuat4f Rotation = FQuat4f::Identity;
	UPROPERTY()
	uint32 CaptureSequence = 0;
	UPROPERTY()
	double CaptureSeconds = 0.0;
	UPROPERTY()
	float ReplicationIntervalSeconds = 0.0f;
	UPROPERTY()
	bool bAwake = true;
};

