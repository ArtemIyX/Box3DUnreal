#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DAsyncJointHandle.h"

/** Break threshold event, sampled at a completed outer physics step. */
struct BOX3DASYNC_API FBox3DAsyncJointBreakEvent
{
	FBox3DAsyncJointHandle Joint;
	uint64 SimulationStep = 0;
};
