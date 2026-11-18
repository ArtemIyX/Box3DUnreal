#include "Box3DTestHelpers.h"
#include "World/Box3DAsyncSubsystem.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DBackendFlagsTest, "Box3D.Backends.EnabledFlags", BOX3D_TEST_FLAGS)

bool FBox3DBackendFlagsTest::RunTest(const FString& Parameters)
{
	UBox3DSettings* Settings = GetMutableDefault<UBox3DSettings>();
	const int32 OriginalBackends = Settings->EnabledBackends;
	ON_SCOPE_EXIT { Settings->EnabledBackends = OriginalBackends; };
	const int32 Sync = static_cast<int32>(EBox3DBackend::Sync);
	const int32 Async = static_cast<int32>(EBox3DBackend::Async);
	for (const EWorldType::Type WorldType : {EWorldType::Game, EWorldType::PIE})
	{
		for (const int32 Flags : {0, Sync, Async, Sync | Async})
		{
			Settings->EnabledBackends = Flags;
			Box3DTest::FTestWorld TestWorld(NAME_None, WorldType);
			UBox3DWorldSubsystem* SyncWorld = TestWorld.World->GetSubsystem<UBox3DWorldSubsystem>();
			UBox3DAsyncSubsystem* AsyncWorld = TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>();
			TestEqual(TEXT("Sync subsystem follows its flag"), SyncWorld != nullptr, (Flags & Sync) != 0);
			TestEqual(TEXT("Async subsystem follows its flag"), AsyncWorld != nullptr, (Flags & Async) != 0);
			if (SyncWorld != nullptr) TestTrue(
				TEXT("Enabled sync world is valid"), b3World_IsValid(SyncWorld->GetBox3DWorldId()));
			if (AsyncWorld != nullptr)
			{
				FBox3DAsyncBodyId Id = 0;
				TestTrue(
					TEXT("Enabled async world accepts work"),
					AsyncWorld->TryCreateStaticBox(FTransform::Identity, FVector(50), Id));
			}
			Settings->EnabledBackends = Flags ^ (Sync | Async);
			TestEqual(TEXT("Settings changes do not remove an existing sync subsystem"),
			          TestWorld.World->GetSubsystem<UBox3DWorldSubsystem>() == SyncWorld, true);
			TestEqual(TEXT("Settings changes do not remove an existing async subsystem"),
			          TestWorld.World->GetSubsystem<UBox3DAsyncSubsystem>() == AsyncWorld, true);
		}
	}
	return true;
}
#endif
