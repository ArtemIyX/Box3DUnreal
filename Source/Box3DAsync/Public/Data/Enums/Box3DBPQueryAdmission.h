#pragma once

#include "CoreMinimal.h"
#include "Box3DBPQueryAdmission.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPQueryAdmission : uint8
{
	Accepted,
	InvalidInput,
	Unsupported,
	InvalidConsumer,
	WrongWorld,
	Capacity,
	Unavailable
};
