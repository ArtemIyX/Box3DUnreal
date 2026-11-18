#pragma once

#include "CoreMinimal.h"
#include "Box3DBPEventKind.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPEventKind : uint8
{
	ContactBegin,
	ContactEnd,
	Impact,
	SensorEnter,
	SensorExit
};
