#pragma once

#include "CoreMinimal.h"
#include "box3d/types.h"

namespace Box3DAsyncStats
{
	struct FWorldSnapshot
	{
		b3Profile Profile{};
		b3Counters Counters{};
		int32 AwakeBodies = 0;
		uint64 CompletedSteps = 0;
		uint64 DeadlineMisses = 0;
		uint64 ClockRebases = 0;
		int32 QueuedCommands = 0;
		int32 OutstandingTraces = 0;
	};

	struct FAggregate
	{
		float StepMs = 0, PairsMs = 0, CollideMs = 0, SolveMs = 0, RefitMs = 0, TransformsMs = 0, SensorsMs = 0;
		uint64 Bodies = 0, AwakeBodies = 0, Shapes = 0, Contacts = 0, Joints = 0, Islands = 0, Tasks = 0, MemoryBytes =
			       0;
		uint64 CompletedSteps = 0, DeadlineMisses = 0, ClockRebases = 0, QueuedCommands = 0, OutstandingTraces = 0;
		int32 WorldCount = 0;
	};

	void Startup();
	void Shutdown();
	BOX3DASYNC_API void RegisterWorld(const void* Key);
	BOX3DASYNC_API void UnregisterWorld(const void* Key);
	BOX3DASYNC_API void Publish(const void* Key, const FWorldSnapshot& Snapshot);
	BOX3DASYNC_API FAggregate GetAggregate();
}
