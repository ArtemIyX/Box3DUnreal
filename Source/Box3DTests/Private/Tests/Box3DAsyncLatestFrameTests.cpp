#include "World/Box3DAsyncSubsystem.h"
#include "Box3DTestHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncLatestFrameTest,
	"Box3DUnreal.Async.LatestWorkerFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncLatestFrameTest::RunTest(const FString& Parameters)
{
	Box3DTest::FTestWorld World(TEXT("AsyncLatestWorkerFrame"));
	UBox3DAsyncSubsystem* Physics = World.World->GetSubsystem<UBox3DAsyncSubsystem>();
	if (!TestNotNull(TEXT("Async subsystem"), Physics)) return false;
	FBox3DAsyncPoseFrame First;
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (FPlatformTime::Seconds() < Deadline && !Physics->TryGetLatestPoseFrame(First))
	{
		FPlatformProcess::Sleep(0.001f);
	}
	if (!TestTrue(TEXT("Worker published a frame"), First.CaptureSerial > 0)) return false;
	TestTrue(TEXT("Capture time is populated"), First.CaptureMonotonicSeconds > 0.0);
	FBox3DAsyncPoseFrame Second;
	const double NextDeadline = FPlatformTime::Seconds() + 2.0;
	while (FPlatformTime::Seconds() < NextDeadline
		&& (!Physics->TryGetLatestPoseFrame(Second) || Second.CaptureSerial <= First.CaptureSerial))
	{
		FPlatformProcess::Sleep(0.001f);
	}
	TestTrue(TEXT("Capture serial advances"), Second.CaptureSerial > First.CaptureSerial);
	TestTrue(TEXT("Capture monotonic time advances"), Second.CaptureMonotonicSeconds > First.CaptureMonotonicSeconds);
	TestTrue(TEXT("Frame step does not exceed completed simulation"),
		Second.SimulationSample.Step <= Physics->GetCompletedStepCount());
	return true;
}
#endif
