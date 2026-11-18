#pragma once

#include "CoreMinimal.h"
#include "Blueprint/Box3DAsyncBlueprint.h"
#include "Box3DAsyncBlueprintTestReceiver.generated.h"

UCLASS()
class UBox3DAsyncBlueprintTestReceiver : public UObject
{
	GENERATED_BODY()
public:
	int32 QueryCount = 0;
	int32 EventCount = 0;
	FBox3DBPQueryResult LastQuery;
	FBox3DBPEventDelivery LastEvent;
	UFUNCTION() void OnQuery(const FBox3DBPQueryResult& Result) { ++QueryCount; LastQuery = Result; }
	UFUNCTION() void OnEvent(const FBox3DBPEventDelivery& Delivery) { ++EventCount; LastEvent = Delivery; }
};
