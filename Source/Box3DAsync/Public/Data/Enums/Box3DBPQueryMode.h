#pragma once

#include "CoreMinimal.h"
#include "Box3DBPQueryMode.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPQueryMode : uint8
{
	Any,
	Closest,
	Multi
};
