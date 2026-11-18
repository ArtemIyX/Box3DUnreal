#include "Box3DAsyncStats.h"

#include "Containers/Ticker.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("Box3DAsync"), STATGROUP_Box3DAsync, STATCAT_Advanced);

DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest steps total (ms)"), STAT_AsyncStep, STATGROUP_Box3DAsync);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest pairs total (ms)"), STAT_AsyncPairs, STATGROUP_Box3DAsync);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest collide total (ms)"), STAT_AsyncCollide, STATGROUP_Box3DAsync);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest solve total (ms)"), STAT_AsyncSolve, STATGROUP_Box3DAsync);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest tree refit total (ms)"), STAT_AsyncRefit, STATGROUP_Box3DAsync);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest transforms total (ms)"), STAT_AsyncTransforms, STATGROUP_Box3DAsync);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Latest sensors total (ms)"), STAT_AsyncSensors, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Worlds"), STAT_AsyncWorlds, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Bodies"), STAT_AsyncBodies, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Awake bodies"), STAT_AsyncAwake, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Shapes"), STAT_AsyncShapes, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Contacts"), STAT_AsyncContacts, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Joints"), STAT_AsyncJoints, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Islands"), STAT_AsyncIslands, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Solver tasks"), STAT_AsyncTasks, STATGROUP_Box3DAsync);
DECLARE_MEMORY_STAT(TEXT("World memory"), STAT_AsyncMemory, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Completed steps (cumulative)"), STAT_AsyncSteps, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Deadline misses (cumulative)"), STAT_AsyncMisses, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Clock rebases (cumulative)"), STAT_AsyncRebases, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Queued commands"), STAT_AsyncCommands, STATGROUP_Box3DAsync);
DECLARE_DWORD_COUNTER_STAT(TEXT("Outstanding traces"), STAT_AsyncTraces, STATGROUP_Box3DAsync);

namespace Box3DAsyncStats
{
	namespace
	{
		FCriticalSection Mutex;
		TMap<const void*, FWorldSnapshot> Worlds;
		FTSTicker::FDelegateHandle Ticker;

		uint32 Dword(uint64 Value) { return static_cast<uint32>(FMath::Min(Value, uint64(MAX_uint32))); }

		bool Tick(float)
		{
#if STATS
			const FAggregate S = GetAggregate();
			SET_FLOAT_STAT(STAT_AsyncStep, S.StepMs);
			SET_FLOAT_STAT(STAT_AsyncPairs, S.PairsMs);
			SET_FLOAT_STAT(STAT_AsyncCollide, S.CollideMs);
			SET_FLOAT_STAT(STAT_AsyncSolve, S.SolveMs);
			SET_FLOAT_STAT(STAT_AsyncRefit, S.RefitMs);
			SET_FLOAT_STAT(STAT_AsyncTransforms, S.TransformsMs);
			SET_FLOAT_STAT(STAT_AsyncSensors, S.SensorsMs);
			SET_DWORD_STAT(STAT_AsyncWorlds, S.WorldCount);
			SET_DWORD_STAT(STAT_AsyncBodies, Dword(S.Bodies));
			SET_DWORD_STAT(STAT_AsyncAwake, Dword(S.AwakeBodies));
			SET_DWORD_STAT(STAT_AsyncShapes, Dword(S.Shapes));
			SET_DWORD_STAT(STAT_AsyncContacts, Dword(S.Contacts));
			SET_DWORD_STAT(STAT_AsyncJoints, Dword(S.Joints));
			SET_DWORD_STAT(STAT_AsyncIslands, Dword(S.Islands));
			SET_DWORD_STAT(STAT_AsyncTasks, Dword(S.Tasks));
			SET_MEMORY_STAT(STAT_AsyncMemory, S.MemoryBytes);
			SET_DWORD_STAT(STAT_AsyncSteps, Dword(S.CompletedSteps));
			SET_DWORD_STAT(STAT_AsyncMisses, Dword(S.DeadlineMisses));
			SET_DWORD_STAT(STAT_AsyncRebases, Dword(S.ClockRebases));
			SET_DWORD_STAT(STAT_AsyncCommands, Dword(S.QueuedCommands));
			SET_DWORD_STAT(STAT_AsyncTraces, Dword(S.OutstandingTraces));
#endif
			return true;
		}
	}

	void Startup() { Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick)); }

	void Shutdown()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		FScopeLock Lock(&Mutex);
		Worlds.Reset();
	}

	void RegisterWorld(const void* Key)
	{
		FScopeLock Lock(&Mutex);
		Worlds.Add(Key, FWorldSnapshot{});
	}

	void UnregisterWorld(const void* Key)
	{
		FScopeLock Lock(&Mutex);
		Worlds.Remove(Key);
	}

	void Publish(const void* Key, const FWorldSnapshot& Snapshot)
	{
		FScopeLock Lock(&Mutex);
		if (FWorldSnapshot* Existing = Worlds.Find(Key)) *Existing = Snapshot;
	}

	FAggregate GetAggregate()
	{
		FScopeLock Lock(&Mutex);
		FAggregate Out;
		Out.WorldCount = Worlds.Num();
		for (const TPair<const void*, FWorldSnapshot>& Entry : Worlds)
		{
			const FWorldSnapshot& S = Entry.Value;
			Out.StepMs += S.Profile.step;
			Out.PairsMs += S.Profile.pairs;
			Out.CollideMs += S.Profile.collide;
			Out.SolveMs += S.Profile.solve;
			Out.RefitMs += S.Profile.refit;
			Out.TransformsMs += S.Profile.transforms;
			Out.SensorsMs += S.Profile.sensors;
			Out.Bodies += FMath::Max(0, S.Counters.bodyCount);
			Out.AwakeBodies += FMath::Max(0, S.AwakeBodies);
			Out.Shapes += FMath::Max(0, S.Counters.shapeCount);
			Out.Contacts += FMath::Max(0, S.Counters.contactCount);
			Out.Joints += FMath::Max(0, S.Counters.jointCount);
			Out.Islands += FMath::Max(0, S.Counters.islandCount);
			Out.Tasks += FMath::Max(0, S.Counters.taskCount);
			Out.MemoryBytes += FMath::Max(0, S.Counters.byteCount);
			Out.CompletedSteps += S.CompletedSteps;
			Out.DeadlineMisses += S.DeadlineMisses;
			Out.ClockRebases += S.ClockRebases;
			Out.QueuedCommands += FMath::Max(0, S.QueuedCommands);
			Out.OutstandingTraces += FMath::Max(0, S.OutstandingTraces);
		}
		return Out;
	}
}
