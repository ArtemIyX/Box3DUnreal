#pragma once

#include "World/Box3DAsyncSubsystem.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "box3d/box3d.h"

struct UBox3DAsyncSubsystem::FImpl
{
	enum class ECommandType : uint8 { CreateBox, CreateSphere, Remove };

	struct FCommand
	{
		ECommandType Type = ECommandType::Remove;
		FBox3DAsyncBodyId BodyId = 0;
		FTransform Transform = FTransform::Identity;
		FVector Dimensions = FVector::ZeroVector;
		FVector InitialVelocity = FVector::ZeroVector;
	};

	struct FTraceRequest
	{
		FBox3DAsyncTraceRequestId RequestId = 0;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	struct FSnapshot
	{
		double ScheduledTime = 0.0;
		TArray<FBox3DAsyncPose> Poses;
	};

	struct FBody
	{
		FBody() = default;

		FBody(b3BodyId InNativeId, bool bInDynamic) : NativeId(InNativeId), bDynamic(bInDynamic)
		{
		}

		b3BodyId NativeId{};
		bool bDynamic = false;
	};

	class FWorker final : public FRunnable
	{
	public:
		explicit FWorker(FImpl& InOwner) : Owner(InOwner)
		{
		}

		virtual uint32 Run() override;

		virtual void Stop() override
		{
			Owner.bStopRequested.Store(true);
			Owner.WakeEvent->Trigger();
		}

	private:
		void ApplyCommands();
		void ExecuteTraces();
		void StepAndPublish(double ScheduledTime, bool bAdvanceSimulation = true);
		FImpl& Owner;
	};

	b3WorldId WorldId{};
	float FixedTimeStep = 1.0f / 60.0f;
	int32 SubStepCount = 1;
	TAtomic<bool> bStopRequested{false};
	FCriticalSection DiagnosticsMutex;
	uint64 CompletedSteps = 0;
	uint64 NativeCreationFailures = 0;
	uint64 DeadlineMisses = 0;
	uint64 ClockRebases = 0;
	double LatestScheduledTime = 0.0;
	double LatestCompletionTime = 0.0;
	FCriticalSection QueueMutex;
	FCriticalSection SnapshotMutex;
	TArray<FCommand> Commands;
	TArray<FTraceRequest> TraceRequests;
	TArray<FBox3DAsyncTraceResult> TraceResults;
	int32 InFlightTraces = 0;
	TSet<FBox3DAsyncBodyId> AcceptedBodyIds;
	FBox3DAsyncBodyId NextBodyId = 1;
	FBox3DAsyncTraceRequestId NextRequestId = 1;
	TArray<TSharedPtr<const FSnapshot, ESPMode::ThreadSafe>> Snapshots;
	TMap<FBox3DAsyncBodyId, FBody> WorkerBodies;
	FEvent* WakeEvent = nullptr;
	TUniquePtr<FWorker> Worker;
	FRunnableThread* Thread = nullptr;
	bool bAccepting = false;

	void Shutdown()
	{
		{
			FScopeLock Lock(&QueueMutex);
			bAccepting = false;
		}
		if (Worker.IsValid())
		{
			Worker->Stop();
		}
		if (Thread != nullptr)
		{
			Thread->WaitForCompletion();
			delete Thread;
			Thread = nullptr;
		}
		Worker.Reset();
		if (b3World_IsValid(WorldId))
		{
			b3DestroyWorld(WorldId);
			WorldId = b3WorldId{};
		}
	}

	~FImpl()
	{
		if (WakeEvent != nullptr)
		{
			FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		}
	}
};

