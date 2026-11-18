#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DIrisBodyItem.h"
#include "Iris/ReplicationState/IrisFastArraySerializer.h"
#include "UObject/SoftObjectPath.h"
#include "Box3DIrisBodyArray.generated.h"

class UBox3DIrisReplicationRoot;

USTRUCT()
struct BOX3DREPLICATION_API FBox3DIrisBodyArray : public FIrisFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FBox3DIrisBodyItem> Items;

	TWeakObjectPtr<UBox3DIrisReplicationRoot> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FBox3DIrisBodyItem, FBox3DIrisBodyArray>(Items, DeltaParms, *this);
	}

	void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);
};

template <>
struct TStructOpsTypeTraits<FBox3DIrisBodyArray> : TStructOpsTypeTraitsBase2<FBox3DIrisBodyArray>
{
	enum { WithNetDeltaSerializer = true };
};

