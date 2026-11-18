#pragma once

#include "CoreMinimal.h"
#include "Box3DBPQueryKind.generated.h"

UENUM(BlueprintType)
enum class EBox3DBPQueryKind : uint8
{
	Line,
	Sweep,
	Overlap,
	AabbCandidates
};
