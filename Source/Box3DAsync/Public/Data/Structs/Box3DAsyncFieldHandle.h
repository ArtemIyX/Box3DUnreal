#pragma once

#include "CoreMinimal.h"

/** A world-scoped, non-reused field identity. */
struct BOX3DASYNC_API FBox3DAsyncFieldHandle
{
	FGuid World;
	uint64 Id = 0;
};
