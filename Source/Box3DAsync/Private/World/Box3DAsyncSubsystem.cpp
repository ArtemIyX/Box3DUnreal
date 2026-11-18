#include "World/Box3DAsyncSubsystem.h"

#include "Box3DAsyncSubsystemImpl.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Simulation/Box3DConversion.h"
#include "World/Box3DSettings.h"

namespace
{
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsFiniteNativeVector(const b3Vec3& Value)
	{
		return FMath::IsFinite(Value.x) && FMath::IsFinite(Value.y) && FMath::IsFinite(Value.z);
	}

	bool IsFiniteNativePosition(const b3Pos& Value)
	{
		return FMath::IsFinite(double(Value.x)) && FMath::IsFinite(double(Value.y)) && FMath::IsFinite(double(Value.z));
	}

	bool IsValidTransform(const FTransform& Transform)
	{
		if (!IsFiniteVector(Transform.GetLocation()) || !Transform.GetRotation().IsNormalized()
			|| !Transform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
		{
			return false;
		}
		const b3Pos Position = Box3D::ToB3Pos(Transform.GetLocation());
		const b3Quat Rotation = Box3D::ToB3(Transform.GetRotation());
		return IsFiniteNativePosition(Position) && FMath::IsFinite(Rotation.v.x) && FMath::IsFinite(Rotation.v.y)
			&& FMath::IsFinite(Rotation.v.z) && FMath::IsFinite(Rotation.s);
	}
}

UBox3DAsyncSubsystem::~UBox3DAsyncSubsystem()
{
	check(IsInGameThread());
	if (Impl.IsValid())
	{
		Impl->Shutdown();
		Impl.Reset();
	}
}

bool UBox3DAsyncSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	check(IsInGameThread());
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	if (World == nullptr || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return false;
	}
	const UBox3DSettings* Settings = GetDefault<UBox3DSettings>();
	// TODO(OUT-OF-SCOPE): Select this backend through shared SimulationBackend settings once the runtime settings and legacy gate are updated [box3d-async-milestone-1].
	return Settings == nullptr || !Settings->bAuthorityOnlySimulation || World->GetNetMode() != NM_Client;
}

void UBox3DAsyncSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	check(IsInGameThread());
	Super::Initialize(Collection);
	Impl = MakeUnique<FImpl>();
	const UBox3DSettings* Settings = GetDefault<UBox3DSettings>();
	if (Settings == nullptr || !FMath::IsFinite(Settings->FixedTimeStep)
		|| Settings->FixedTimeStep < 0.001f || Settings->FixedTimeStep > 1.0f || !IsFiniteVector(Settings->Gravity))
	{
		return;
	}

	Impl->FixedTimeStep = Settings->FixedTimeStep;
	Impl->SubStepCount = FMath::Clamp(Settings->SubStepCount, 1, 16);
	const b3Vec3 NativeGravity = Box3D::ToB3Accel(Settings->Gravity);
	if (!IsFiniteNativeVector(NativeGravity))
	{
		return;
	}
	b3WorldDef WorldDef = b3DefaultWorldDef();
	WorldDef.gravity = NativeGravity;
	WorldDef.enableSleep = Settings->bEnableSleep;
	WorldDef.enableContinuous = Settings->bEnableContinuous;
	WorldDef.workerCount = 1;
	Impl->WorldId = b3CreateWorld(&WorldDef);
	if (!b3World_IsValid(Impl->WorldId))
	{
		return;
	}

	Impl->WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
	Impl->bAccepting = Impl->WakeEvent != nullptr;
	if (!Impl->bAccepting)
	{
		b3DestroyWorld(Impl->WorldId);
		Impl->WorldId = b3WorldId{};
		return;
	}
	Impl->Worker = MakeUnique<FImpl::FWorker>(*Impl);
	Impl->Thread = FRunnableThread::Create(Impl->Worker.Get(), TEXT("Box3DAsyncWorld"), 0, TPri_AboveNormal);
	if (Impl->Thread == nullptr)
	{
		Impl->bAccepting = false;
		Impl->Worker.Reset();
		FPlatformProcess::ReturnSynchEventToPool(Impl->WakeEvent);
		Impl->WakeEvent = nullptr;
		b3DestroyWorld(Impl->WorldId);
		Impl->WorldId = b3WorldId{};
	}
}

void UBox3DAsyncSubsystem::Deinitialize()
{
	check(IsInGameThread());
	if (Impl.IsValid())
	{
		Impl->Shutdown();
	}
	Impl.Reset();
	Super::Deinitialize();
}

bool UBox3DAsyncSubsystem::TryCreateStaticBox(const FTransform& Transform, const FVector& HalfExtents,
                                              FBox3DAsyncBodyId& OutBodyId)
{
	check(IsInGameThread());
	OutBodyId = 0;
	const b3Vec3 NativeHalfExtents = Box3D::ToB3(HalfExtents);
	if (!Impl.IsValid() || !IsValidTransform(Transform) || !IsFiniteVector(HalfExtents)
		|| HalfExtents.X <= 0.0 || HalfExtents.Y <= 0.0 || HalfExtents.Z <= 0.0
		|| !IsFiniteNativeVector(NativeHalfExtents) || NativeHalfExtents.x <= 0.0f || NativeHalfExtents.y <= 0.0f ||
		NativeHalfExtents.z <= 0.0f)
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0)
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::CreateBox;
	Command.BodyId = Impl->NextBodyId++;
	Command.Transform = Transform;
	Command.Dimensions = HalfExtents;
	Impl->AcceptedBodyIds.Add(Command.BodyId);
	OutBodyId = Command.BodyId;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryCreateDynamicSphere(const FTransform& Transform, float Radius,
                                                  const FVector& InitialVelocity, FBox3DAsyncBodyId& OutBodyId)
{
	check(IsInGameThread());
	OutBodyId = 0;
	const float NativeRadius = Radius * Box3D::UEToMeters;
	const b3Vec3 NativeVelocity = Box3D::ToB3(InitialVelocity);
	if (!Impl.IsValid() || !IsValidTransform(Transform) || !FMath::IsFinite(Radius) || Radius <= 0.0f
		|| !FMath::IsFinite(NativeRadius) || NativeRadius <= 0.0f || !IsFiniteVector(InitialVelocity)
		|| !IsFiniteNativeVector(NativeVelocity))
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || Impl->NextBodyId == 0)
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::CreateSphere;
	Command.BodyId = Impl->NextBodyId++;
	Command.Transform = Transform;
	Command.Dimensions.X = Radius;
	Command.InitialVelocity = InitialVelocity;
	Impl->AcceptedBodyIds.Add(Command.BodyId);
	OutBodyId = Command.BodyId;
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TryRemoveBody(FBox3DAsyncBodyId BodyId)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || BodyId == 0)
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->Commands.Num() >= CommandCapacity || !Impl->AcceptedBodyIds.Contains(BodyId))
	{
		return false;
	}
	FImpl::FCommand& Command = Impl->Commands.AddDefaulted_GetRef();
	Command.Type = FImpl::ECommandType::Remove;
	Command.BodyId = BodyId;
	Impl->AcceptedBodyIds.Remove(BodyId);
	Impl->WakeEvent->Trigger();
	return true;
}

bool UBox3DAsyncSubsystem::TrySubmitLineTrace(const FVector& Start, const FVector& End,
                                              FBox3DAsyncTraceRequestId& OutRequestId)
{
	check(IsInGameThread());
	OutRequestId = 0;
	const b3Vec3 NativeDelta = Box3D::ToB3(End - Start);
	const b3Pos NativeStart = Box3D::ToB3Pos(Start);
	const b3Pos NativeEnd = Box3D::ToB3Pos(End);
	if (!Impl.IsValid() || !IsFiniteVector(Start) || !IsFiniteVector(End) || Start.Equals(End, KINDA_SMALL_NUMBER))
	{
		return false;
	}
	if (!IsFiniteNativePosition(NativeStart) || !IsFiniteNativePosition(NativeEnd) || !IsFiniteNativeVector(NativeDelta)
		|| (NativeDelta.x == 0.0f && NativeDelta.y == 0.0f && NativeDelta.z == 0.0f))
	{
		return false;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	const int32 Outstanding = Impl->TraceRequests.Num() + Impl->InFlightTraces + Impl->TraceResults.Num();
	if (!Impl->bAccepting || Outstanding >= OutstandingTraceCapacity || Impl->NextRequestId == 0)
	{
		return false;
	}
	FImpl::FTraceRequest& Request = Impl->TraceRequests.AddDefaulted_GetRef();
	Request.RequestId = Impl->NextRequestId++;
	Request.Start = Start;
	Request.End = End;
	OutRequestId = Request.RequestId;
	Impl->WakeEvent->Trigger();
	return true;
}

void UBox3DAsyncSubsystem::DrainTraceResults(TArray<FBox3DAsyncTraceResult>& OutResults)
{
	check(IsInGameThread());
	OutResults.Reset();
	if (!Impl.IsValid())
	{
		return;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	Swap(OutResults, Impl->TraceResults);
}

void UBox3DAsyncSubsystem::SamplePoses(TArray<FBox3DAsyncPose>& OutPoses, double* OutSampleTimeSeconds) const
{
	check(IsInGameThread());
	OutPoses.Reset();
	if (!Impl.IsValid())
	{
		if (OutSampleTimeSeconds != nullptr)
		{
			*OutSampleTimeSeconds = 0.0;
		}
		return;
	}
	TArray<TSharedPtr<const FImpl::FSnapshot, ESPMode::ThreadSafe>> LocalSnapshots;
	{
		FScopeLock Lock(&Impl->SnapshotMutex);
		LocalSnapshots = Impl->Snapshots;
	}
	if (LocalSnapshots.IsEmpty())
	{
		if (OutSampleTimeSeconds != nullptr)
		{
			*OutSampleTimeSeconds = FPlatformTime::Seconds() - 2.0 * Impl->FixedTimeStep;
		}
		return;
	}
	const double TargetTime = FPlatformTime::Seconds() - 2.0 * Impl->FixedTimeStep;
	if (OutSampleTimeSeconds != nullptr)
	{
		*OutSampleTimeSeconds = TargetTime;
	}
	const FImpl::FSnapshot* Before = LocalSnapshots[0].Get();
	const FImpl::FSnapshot* After = nullptr;
	for (const TSharedPtr<const FImpl::FSnapshot, ESPMode::ThreadSafe>& SnapshotPtr : LocalSnapshots)
	{
		const FImpl::FSnapshot& Snapshot = *SnapshotPtr;
		if (Snapshot.ScheduledTime <= TargetTime)
		{
			Before = &Snapshot;
		}
		else
		{
			After = &Snapshot;
			break;
		}
	}
	if (After == nullptr || TargetTime <= LocalSnapshots[0]->ScheduledTime)
	{
		OutPoses = Before->Poses;
		return;
	}
	const double Duration = After->ScheduledTime - Before->ScheduledTime;
	const float Alpha = Duration > 0.0
		                    ? float(FMath::Clamp((TargetTime - Before->ScheduledTime) / Duration, 0.0, 1.0))
		                    : 0.0f;
	int32 EarlierIndex = 0;
	int32 LaterIndex = 0;
	while (EarlierIndex < Before->Poses.Num() && LaterIndex < After->Poses.Num())
	{
		const FBox3DAsyncPose& Earlier = Before->Poses[EarlierIndex];
		const FBox3DAsyncPose& Later = After->Poses[LaterIndex];
		if (Earlier.BodyId == Later.BodyId)
		{
			FBox3DAsyncPose& Pose = OutPoses.Add_GetRef(Earlier);
			Pose.Transform.SetLocation(FMath::Lerp(Earlier.Transform.GetLocation(), Later.Transform.GetLocation(),
			                                       Alpha));
			Pose.Transform.SetRotation(
				FQuat::Slerp(Earlier.Transform.GetRotation(), Later.Transform.GetRotation(), Alpha).GetNormalized());
			++EarlierIndex;
			++LaterIndex;
		}
		else if (Earlier.BodyId < Later.BodyId)
		{
			if (Alpha < 1.0f)
			{
				OutPoses.Add(Earlier);
			}
			++EarlierIndex;
		}
		else
		{
			if (Alpha >= 1.0f)
			{
				OutPoses.Add(Later);
			}
			++LaterIndex;
		}
	}
	for (; EarlierIndex < Before->Poses.Num(); ++EarlierIndex)
	{
		if (Alpha < 1.0f)
		{
			OutPoses.Add(Before->Poses[EarlierIndex]);
		}
	}
	for (; LaterIndex < After->Poses.Num(); ++LaterIndex)
	{
		if (Alpha >= 1.0f)
		{
			OutPoses.Add(After->Poses[LaterIndex]);
		}
	}
}

uint64 UBox3DAsyncSubsystem::GetCompletedStepCount() const
{
	check(IsInGameThread());
	if (!Impl.IsValid())
	{
		return 0;
	}
	FScopeLock Lock(&Impl->DiagnosticsMutex);
	return Impl->CompletedSteps;
}

FBox3DAsyncDiagnostics UBox3DAsyncSubsystem::GetDiagnostics() const
{
	check(IsInGameThread());
	FBox3DAsyncDiagnostics Result;
	if (!Impl.IsValid())
	{
		return Result;
	}
	{
		FScopeLock Lock(&Impl->DiagnosticsMutex);
		Result.CompletedSteps = Impl->CompletedSteps;
		Result.NativeCreationFailures = Impl->NativeCreationFailures;
		Result.DeadlineMisses = Impl->DeadlineMisses;
		Result.ClockRebases = Impl->ClockRebases;
		Result.LatestScheduledTimeSeconds = Impl->LatestScheduledTime;
		Result.LatestCompletionTimeSeconds = Impl->LatestCompletionTime;
	}
	FScopeLock Lock(&Impl->QueueMutex);
	Result.QueuedCommands = Impl->Commands.Num();
	Result.OutstandingTraces = Impl->TraceRequests.Num() + Impl->InFlightTraces + Impl->TraceResults.Num();
	return Result;
}

