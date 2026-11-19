#include "Box3DReplication.h"
#include "Box3DIrisReplication.h"
#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Modules/ModuleManager.h"

void FBox3DReplicationModule::StartupModule()
{
    FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("IrisCore"));
    UE::Net::FNetObjectFactoryRegistry::RegisterFactory(UBox3DIrisNetObjectFactory::StaticClass(), Box3DIrisReplication::FactoryName);
    check(UE::Net::FNetObjectFactoryRegistry::GetFactoryIdFromName(Box3DIrisReplication::FactoryName) != UE::Net::InvalidNetObjectFactoryId);
}

void FBox3DReplicationModule::ShutdownModule()
{
    if (FModuleManager::Get().IsModuleLoaded(TEXT("IrisCore")))
        UE::Net::FNetObjectFactoryRegistry::UnregisterFactory(Box3DIrisReplication::FactoryName);
}

IMPLEMENT_MODULE(FBox3DReplicationModule, Box3DReplication)
