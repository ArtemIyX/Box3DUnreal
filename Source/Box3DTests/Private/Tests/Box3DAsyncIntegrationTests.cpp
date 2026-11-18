#include "Box3DTestHelpers.h"
#include "World/Box3DAsyncSubsystem.h"
#include "World/Box3DSettings.h"
#include "Simulation/Box3DConversion.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "box3d/box3d.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	struct FScopedBackendSettings
	{
		UBox3DSettings* Settings = GetMutableDefault<UBox3DSettings>();
		int32 Backends = Settings->EnabledBackends;
		bool bAuthorityOnly = Settings->bAuthorityOnlySimulation;
		FVector Gravity = Settings->Gravity;
		~FScopedBackendSettings()
		{
			Settings->EnabledBackends = Backends;
			Settings->bAuthorityOnlySimulation = bAuthorityOnly;
			Settings->Gravity = Gravity;
		}
	};

	bool AwaitQuery(UBox3DAsyncSubsystem& Physics, FBox3DAsyncQueryConsumer Consumer,
	                FBox3DAsyncQueryHandle Request, FBox3DAsyncQueryResult& Out)
	{
		const double Deadline = FPlatformTime::Seconds() + 5.0;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (Physics.TryTakeQueryResult(Consumer, Request, Out)) return true;
			FPlatformProcess::Sleep(0.005f);
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncBackendIsolation,
	"Box3D.Async.BackendIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncBackendIsolation::RunTest(const FString& Parameters)
{
	FScopedBackendSettings Restore;
	Restore.Settings->bAuthorityOnlySimulation = false;
	Restore.Settings->Gravity = FVector::ZeroVector;
	const int32 SyncFlag = static_cast<int32>(EBox3DBackend::Sync);
	const int32 AsyncFlag = static_cast<int32>(EBox3DBackend::Async);
	for (EWorldType::Type WorldType : {EWorldType::Game, EWorldType::PIE})
	{
		for (int32 Flags : {0, SyncFlag, AsyncFlag, SyncFlag | AsyncFlag})
		{
			Restore.Settings->EnabledBackends = Flags;
			Box3DTest::FTestWorld World(FName(*FString::Printf(TEXT("AsyncBackend_%d_%d"), int32(WorldType), Flags)), WorldType);
			UBox3DWorldSubsystem* Sync = World.World->GetSubsystem<UBox3DWorldSubsystem>();
			UBox3DAsyncSubsystem* Async = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
			const FString Label = FString::Printf(TEXT("type %d flags %d"), int32(WorldType), Flags);
			TestEqual(*FString::Printf(TEXT("Sync presence %s"), *Label), Sync != nullptr, (Flags & SyncFlag) != 0);
			TestEqual(*FString::Printf(TEXT("Async presence %s"), *Label), Async != nullptr, (Flags & AsyncFlag) != 0);
			if (Sync && Async)
			{
				const b3WorldId Native = Sync->GetBox3DWorldId();
				if (!TestTrue(TEXT("Sync world valid"), b3World_IsValid(Native))) return false;
				b3BodyDef Def = b3DefaultBodyDef();
				Def.position = Box3D::ToB3Pos(FVector(300, 0, 0));
				const b3BodyId NativeBody = b3CreateBody(Native, &Def);
				b3ShapeDef ShapeDef = b3DefaultShapeDef();
				const b3Sphere Sphere{{0, 0, 0}, 0.5f};
				b3CreateSphereShape(NativeBody, &ShapeDef, &Sphere);
				FBox3DAsyncBodyId Body = 0;
				if (!TestTrue(TEXT("Async box accepted"), Async->TryCreateStaticBox(FTransform::Identity, FVector(50), Body))) return false;
				bool bCreated = false;
				const double Deadline = FPlatformTime::Seconds() + 5.0;
				while (!bCreated && FPlatformTime::Seconds() < Deadline)
				{
					TArray<FBox3DAsyncCreationResult> Results;
					Async->DrainCreationResults(Results);
					for (const auto& Result : Results) if (Result.ResourceId == Body) bCreated = Result.bSucceeded;
					if (!bCreated) FPlatformProcess::Sleep(0.005f);
				}
				if (!TestTrue(TEXT("Async box created"), bCreated)) return false;
				FBox3DAsyncQueryConsumer Consumer;
				if (!TestTrue(TEXT("Register query consumer"), Async->TryRegisterQueryConsumer(Consumer))) return false;
				FBox3DAsyncQueryDesc Query;
				Query.Start = FVector(-100, 0, 0); Query.End = FVector(100, 0, 0);
				FBox3DAsyncQueryHandle Request;
				FBox3DAsyncQueryResult Result;
				if (!TestTrue(TEXT("Submit async local query"), Async->TrySubmitQuery(Consumer, Query, Request))
					|| !TestTrue(TEXT("Take async local query"), AwaitQuery(*Async, Consumer, Request, Result))) return false;
				TestTrue(TEXT("Async sees only its own box"), Result.Status == EBox3DAsyncQueryStatus::Hit && Result.Hits.Num() == 1 && Result.Hits[0].BodyId == Body);
				const b3QueryFilter Filter = b3DefaultQueryFilter();
				const b3RayResult SyncMiss = b3World_CastRayClosest(Native, Box3D::ToB3Pos(Query.Start), Box3D::ToB3(Query.End - Query.Start), Filter);
				TestFalse(TEXT("Sync does not see async box"), b3Shape_IsValid(SyncMiss.shapeId));
				Query.Start = FVector(200, 0, 0); Query.End = FVector(400, 0, 0);
				const b3RayResult SyncHit = b3World_CastRayClosest(Native, Box3D::ToB3Pos(Query.Start), Box3D::ToB3(Query.End - Query.Start), Filter);
				TestTrue(TEXT("Sync sees own sphere"), b3Shape_IsValid(SyncHit.shapeId));
				if (!TestTrue(TEXT("Submit async remote query"), Async->TrySubmitQuery(Consumer, Query, Request))
					|| !TestTrue(TEXT("Take async remote query"), AwaitQuery(*Async, Consumer, Request, Result))) return false;
				TestTrue(TEXT("Async does not see sync sphere"), Result.Status == EBox3DAsyncQueryStatus::Miss);
				Async->UnregisterQueryConsumer(Consumer);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncCombinedLoad,
	"Box3D.Async.CombinedLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncCombinedLoad::RunTest(const FString& Parameters)
{
	FScopedBackendSettings Restore;
	Restore.Settings->EnabledBackends = int32(EBox3DBackend::Sync) | int32(EBox3DBackend::Async);
	Restore.Settings->bAuthorityOnlySimulation = false;
	Restore.Settings->Gravity = FVector::ZeroVector;
	Box3DTest::FTestWorld World(TEXT("AsyncCombinedLoad"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async world"), Physics)) return false;
	FBox3DAsyncShapeOptions SensorOptions; SensorOptions.bSensor = true;
	FBox3DAsyncShapeOptions VisitorOptions; VisitorOptions.bDetectableBySensors = true;
	FBox3DAsyncBodyHandle Sensor;
	if (!TestTrue(TEXT("Sensor accepted"), Physics->TryCreateStaticBox(FTransform::Identity, FVector(10000), SensorOptions, Sensor))) return false;
	constexpr int32 VisitorCount = 24;
	for (int32 Index = 0; Index < VisitorCount; ++Index)
	{
		FBox3DAsyncBodyHandle Body;
		if (!TestTrue(TEXT("Visitor accepted"), Physics->TryCreateDynamicSphere(
			FTransform(FVector(-2300 + Index * 200, 0, 0)), 20, FVector::ZeroVector, VisitorOptions, Body))) return false;
	}
	int32 Created = 0;
	const double CreationDeadline = FPlatformTime::Seconds() + 5.0;
	while (Created < VisitorCount + 1 && FPlatformTime::Seconds() < CreationDeadline)
	{
		TArray<FBox3DAsyncCreationResult> Results;
		Physics->DrainCreationResults(Results);
		for (const auto& Result : Results)
		{
			if (!TestTrue(TEXT("Load body created"), Result.bSucceeded)) return false;
			++Created;
		}
		if (Created < VisitorCount + 1) FPlatformProcess::Sleep(0.005f);
	}
	if (!TestEqual(TEXT("All load bodies acknowledged"), Created, VisitorCount + 1)) return false;
	FBox3DAsyncEventSubscription Events;
	FBox3DAsyncEventFilter Filter; Filter.bContacts = false; Filter.bImpacts = false;
	FBox3DAsyncQueryConsumer Consumer;
	if (!TestTrue(TEXT("Subscribe"), Physics->SubscribeEvents(Filter, Events))
		|| !TestTrue(TEXT("Query consumer"), Physics->TryRegisterQueryConsumer(Consumer))) return false;
	const double Begin = FPlatformTime::Seconds();
	int32 Completed = 0;
	int32 ObservedVisitors = 0;
	FBox3DAsyncQueryDesc Query;
	Query.Start = FVector(-2500, 0, 0); Query.End = FVector(2500, 0, 0);
	Query.Mode = EBox3DAsyncLineMode::Multi; Query.MaxHits = 8;
	for (int32 Batch = 0; Batch < 4; ++Batch)
	{
		TArray<FBox3DAsyncQueryHandle> Requests;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			FBox3DAsyncQueryHandle Request;
			if (!TestTrue(TEXT("Load query accepted"), Physics->TrySubmitQuery(Consumer, Query, Request))) return false;
			Requests.Add(Request);
		}
		for (FBox3DAsyncQueryHandle Request : Requests)
		{
			FBox3DAsyncQueryResult Result;
			if (!TestTrue(TEXT("Load query completed"), AwaitQuery(*Physics, Consumer, Request, Result))) return false;
			TestTrue(TEXT("Load query observes solid shapes"), Result.Status == EBox3DAsyncQueryStatus::Truncated && Result.Hits.Num() == 8);
			++Completed;
		}
	}
	const double EventDeadline = FPlatformTime::Seconds() + 5.0;
	while (ObservedVisitors < VisitorCount && FPlatformTime::Seconds() < EventDeadline)
	{
		FBox3DAsyncEventDelivery Delivery;
		if (!Physics->PollEvents(Events, Delivery)) return false;
		if (Delivery.Status == EBox3DAsyncEventPollStatus::NeedsResync || Delivery.Status == EBox3DAsyncEventPollStatus::Unavailable)
		{
			FPlatformProcess::Sleep(0.005f);
			continue;
		}
		for (const auto& Event : Delivery.Batch.Events)
			if (Event.Kind == EBox3DAsyncEventKind::SensorEnter && Event.BodyA == Sensor.Id) ++ObservedVisitors;
		if (ObservedVisitors < VisitorCount) FPlatformProcess::Sleep(0.005f);
	}
	const FBox3DAsyncDiagnostics Diagnostics = Physics->GetDiagnostics();
	TestEqual(TEXT("All queries delivered"), Completed, 48);
	TestEqual(TEXT("All visitor relationships observed"), ObservedVisitors, VisitorCount);
	TestTrue(TEXT("Physics continued stepping"), Diagnostics.CompletedSteps > 0);
	TestTrue(TEXT("Native query traversals measured"), Diagnostics.ExecutedQueries >= uint64(Completed) && Diagnostics.MaxQueryExecutionSeconds > 0.0);
	AddInfo(FString::Printf(TEXT("Combined fixture: %d queries, %d sensor relationships, %.3f ms elapsed, %.3f ms max native query, %llu deadline misses"),
		Completed, ObservedVisitors, (FPlatformTime::Seconds() - Begin) * 1000.0,
		Diagnostics.MaxQueryExecutionSeconds * 1000.0, Diagnostics.DeadlineMisses));
	Physics->UnregisterQueryConsumer(Consumer);
	Physics->UnsubscribeEvents(Events);
	return true;
}
#endif
