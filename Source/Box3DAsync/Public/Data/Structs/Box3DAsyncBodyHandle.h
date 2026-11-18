#pragma once

#include "CoreMinimal.h"

/** Generation-safe world-scoped body identity. */
struct BOX3DASYNC_API FBox3DAsyncBodyHandle
{
	FGuid World;
	uint64 Id = 0;
};
