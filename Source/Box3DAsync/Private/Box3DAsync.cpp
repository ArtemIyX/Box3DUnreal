#include "Box3DAsync.h"
#include "World/Box3DAsyncStats.h"

#define LOCTEXT_NAMESPACE "FBox3DAsyncModule"

void FBox3DAsyncModule::StartupModule()
{
	Box3DAsyncStats::Startup();
}

void FBox3DAsyncModule::ShutdownModule()
{
	Box3DAsyncStats::Shutdown();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBox3DAsyncModule, Box3DAsync)
