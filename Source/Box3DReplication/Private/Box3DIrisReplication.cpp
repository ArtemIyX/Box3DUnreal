#include "Box3DIrisReplication.h"
#include "Box3DIrisReplicationSubsystem.h"
#include "Box3DReplicationSettings.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationFragmentUtil.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Net/UnrealNetwork.h"
#include "HAL/UnrealMemory.h"

namespace
{
	constexpr uint8 FactoryHeaderVersion = 2;

	class FBox3DIrisCreationHeader final : public UE::Net::FNetObjectCreationHeader
	{
	public:
		uint8 Version = FactoryHeaderVersion;
		FGuid WorldEpoch;
		FVector NetworkOrigin = FVector::ZeroVector;
		uint32 OriginRevision = 0;
	};

	void WriteUInt64(UE::Net::FNetBitStreamWriter& Writer, uint64 Value)
	{
		Writer.WriteBits(uint32(Value), 32);
		Writer.WriteBits(uint32(Value >> 32), 32);
	}

	uint64 ReadUInt64(UE::Net::FNetBitStreamReader& Reader)
	{
		const uint64 Low = Reader.ReadBits(32);
		const uint64 High = Reader.ReadBits(32);
		return Low | (High << 32);
	}
}

const FName Box3DIrisReplication::FactoryName(TEXT("Box3DIrisBodyRoot"));

bool Box3DIrisReplication::TryQuantizePositionToCentimeter(const FVector& Value, FVector& OutQuantized)
{
	const auto Quantize = [](double Component, double& OutComponent)
	{
		if (!FMath::IsFinite(Component)) return false;
		const double Magnitude = FMath::FloorToDouble(FMath::Abs(Component) + 0.5);
		OutComponent = Component < 0.0 ? -Magnitude : Magnitude;
		return FMath::IsFinite(OutComponent);
	};
	FVector Quantized;
	if (!Quantize(Value.X, Quantized.X) || !Quantize(Value.Y, Quantized.Y) || !Quantize(Value.Z, Quantized.Z)) return false;
	OutQuantized = Quantized;
	return true;
}

bool Box3DIrisReplication::ShouldSuspendForAuthorityOriginShift(const FVector& FrozenOrigin,
	const FVector& CurrentOrigin, bool bAlreadySuspended)
{
	return bAlreadySuspended || !CurrentOrigin.Equals(FrozenOrigin, 0.0);
}

FVector Box3DIrisReplication::ToAbsoluteWorldPosition(const FVector& LocalPosition, const FVector& WorldOrigin)
{
	return LocalPosition + WorldOrigin;
}

FVector Box3DIrisReplication::ToLocalWorldPosition(const FVector& AbsolutePosition, const FVector& WorldOrigin)
{
	return AbsolutePosition - WorldOrigin;
}

int32 Box3DIrisReplication::ComputeNextCandidateCursor(int32 StartIndex, int32 CandidateCount,
	int32 TotalCount, int32 FirstDeferredCandidate)
{
	if (TotalCount <= 0) return 0;
	const int32 Offset = FirstDeferredCandidate >= 0 ? FirstDeferredCandidate : CandidateCount;
	return (StartIndex + FMath::Clamp(Offset, 0, CandidateCount)) % TotalCount;
}

bool Box3DIrisReplication::IsAdmissionPreferred(double CandidateDistance, uint64 CandidateId,
	double ResidentDistance, uint64 ResidentId)
{
	return CandidateDistance < ResidentDistance
		|| (CandidateDistance == ResidentDistance && CandidateId < ResidentId);
}

Box3DIrisReplication::FStageBudget Box3DIrisReplication::ComputeStageBudget(int32 LifecycleCount,
	int32 RoutineCount, int32 TotalBudget, int32 MaxLifecycleBudget)
{
	FStageBudget Result;
	const int32 SafeTotalBudget = FMath::Max(0, TotalBudget);
	Result.Lifecycle = FMath::Min(FMath::Max(0, LifecycleCount),
		FMath::Min(SafeTotalBudget, FMath::Max(0, MaxLifecycleBudget)));
	Result.Routine = FMath::Min(FMath::Max(0, RoutineCount), SafeTotalBudget - Result.Lifecycle);
	return Result;
}

TArray<Box3DIrisReplication::FDistanceTier> Box3DIrisReplication::GetDistanceTiers()
{
	TArray<FDistanceTier> Tiers;
	for (const TPair<float, float>& Pair : GetDefault<UBox3DReplicationSettings>()->DistanceRatesHz)
	{
		if (FMath::IsFinite(Pair.Key) && Pair.Key > 0.0f && FMath::IsFinite(Pair.Value) && Pair.Value > 0.0f)
			Tiers.Add({double(Pair.Key), 1.0 / double(Pair.Value)});
	}
	Tiers.Sort([](const FDistanceTier& A, const FDistanceTier& B) { return A.MaxDistance < B.MaxDistance; });
	if (Tiers.IsEmpty()) Tiers = {{1500.0, 1.0 / 60.0}, {2500.0, 1.0 / 45.0},
		{4000.0, 1.0 / 30.0}, {7000.0, 1.0 / 15.0}, {10000.0, 1.0 / 5.0}};
	return Tiers;
}

double Box3DIrisReplication::ComputeTierInterval(double Distance, double PreviousInterval)
{
	const UBox3DReplicationSettings* Settings = GetDefault<UBox3DReplicationSettings>();
	return ComputeTierInterval(Distance, PreviousInterval, GetDistanceTiers(), Settings->TierHysteresis);
}

double Box3DIrisReplication::ComputeTierInterval(double Distance, double PreviousInterval,
	const TArray<FDistanceTier>& Tiers, double Hysteresis)
{
	if (Tiers.IsEmpty()) return 1.0 / 5.0;
	Hysteresis = FMath::IsFinite(Hysteresis) ? FMath::Max(0.0, Hysteresis) : 0.0;
	for (int32 Index = 0; Index < Tiers.Num(); ++Index)
	{
		const double Lower = Index == 0 ? 0.0 : Tiers[Index - 1].MaxDistance;
		const double Upper = Tiers[Index].MaxDistance;
		if (PreviousInterval == Tiers[Index].Interval && (Index == 0 || Distance > Lower - Hysteresis)
			&& (Index == Tiers.Num() - 1 || Distance <= Upper + Hysteresis)) return Tiers[Index].Interval;
	}
	for (const FDistanceTier& Tier : Tiers)
		if (Distance <= Tier.MaxDistance) return Tier.Interval;
	return Tiers.Last().Interval;
}

bool Box3DIrisReplication::IsDeadlineDue(double Now, double Deadline)
{
	return Now >= Deadline;
}

double Box3DIrisReplication::AdvanceDeadline(double Now, double Interval, double Deadline)
{
	if (!FMath::IsFinite(Interval) || Interval <= 0.0) return Now;
	if (Deadline > Now) return Deadline;
	const double Steps = FMath::FloorToDouble((Now - Deadline) / Interval) + 1.0;
	return Deadline + Steps * Interval;
}

bool Box3DIrisReplication::IsCaptureSequenceNewer(uint32 NewSequence, uint32 PreviousSequence)
{
	return int32(NewSequence - PreviousSequence) > 0;
}

bool Box3DIrisReplication::ShouldResetPresentation(uint64 BodyId, uint32 RelevanceRevision,
	uint32 DiscontinuityRevision, uint64 PreviousBodyId, uint32 PreviousRelevanceRevision,
	uint32 PreviousDiscontinuityRevision, bool bHasPrevious)
{
	return !bHasPrevious || BodyId != PreviousBodyId || RelevanceRevision != PreviousRelevanceRevision
		|| DiscontinuityRevision != PreviousDiscontinuityRevision;
}

float Box3DIrisReplication::ComputePlaybackAlpha(double PlaybackTime, double BeforeTime, double AfterTime)
{
	if (AfterTime <= BeforeTime) return 1.0f;
	return float(FMath::Clamp((PlaybackTime - BeforeTime) / (AfterTime - BeforeTime), 0.0, 1.0));
}

double Box3DIrisReplication::PreserveMonotonicPlaybackTime(double CandidateTime, double PreviousTime)
{
	return FMath::Max(CandidateTime, PreviousTime);
}

UBox3DIrisReplicationRoot::UBox3DIrisReplicationRoot()
{
	BodyArray.Owner = this;
}

UWorld* UBox3DIrisReplicationRoot::GetWorld() const
{
	return ReplicationWorld.Get();
}

void UBox3DIrisReplicationRoot::RegisterReplicationFragments(
	UE::Net::FFragmentRegistrationContext& Context,
	UE::Net::EFragmentRegistrationFlags RegistrationFlags)
{
	UE::Net::FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(this, Context, RegistrationFlags);
}

void UBox3DIrisReplicationRoot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBox3DIrisReplicationRoot, BodyArray);
	DOREPLIFETIME(UBox3DIrisReplicationRoot, WorldEpoch);
	DOREPLIFETIME(UBox3DIrisReplicationRoot, NetworkOrigin);
	DOREPLIFETIME(UBox3DIrisReplicationRoot, OriginRevision);
}

void UBox3DIrisReplicationRoot::SetNetworkMetadata(const FGuid& InWorldEpoch, const FVector& InNetworkOrigin,
	uint32 InOriginRevision)
{
	WorldEpoch = InWorldEpoch;
	NetworkOrigin = InNetworkOrigin;
	OriginRevision = InOriginRevision;
}

void UBox3DIrisReplicationRoot::ActivateInitialState()
{
	bInitialStateReady = true;
	OnReceivedItems();
}

void UBox3DIrisReplicationRoot::DetachAndClear()
{
	BodyArray.Items.Reset();
	BodyArray.Owner = nullptr;
	bInitialStateReady = false;
}

void UBox3DIrisReplicationRoot::OnReceivedItems()
{
	if (!bInitialStateReady) return;
	++ReceiveRevision;
	// Presentation is intentionally owned by the game-thread replication subsystem.
}

void FBox3DIrisBodyArray::PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	if (UBox3DIrisReplicationRoot* Root = Owner.Get()) Root->OnReceivedItems();
}

UNetObjectFactory::FInstantiateResult UBox3DIrisNetObjectFactory::InstantiateReplicatedObjectFromHeader(
	const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header)
{
	FInstantiateResult Result;
	const FBox3DIrisCreationHeader* CreationHeader = static_cast<const FBox3DIrisCreationHeader*>(Header);
	if (CreationHeader == nullptr || CreationHeader->Version != FactoryHeaderVersion) return Result;
	UEngineReplicationBridge* EngineBridge = Cast<UEngineReplicationBridge>(Bridge);
	UNetDriver* NetDriver = EngineBridge ? EngineBridge->GetNetDriver() : nullptr;
	UWorld* World = NetDriver ? NetDriver->GetWorld() : nullptr;
	if (World == nullptr) return Result;
	UBox3DIrisReplicationRoot* Root = NewObject<UBox3DIrisReplicationRoot>(GetTransientPackage());
	Root->SetReplicationWorld(World);
	Root->SetNetworkMetadata(CreationHeader->WorldEpoch, CreationHeader->NetworkOrigin, CreationHeader->OriginRevision);
	Result.Instance = Root;
	Result.Flags = EReplicationBridgeCreateNetRefHandleResultFlags::AllowDestroyInstanceFromRemote;
	return Result;
}

void UBox3DIrisNetObjectFactory::PostInit(const FPostInitContext& Context)
{
	if (UBox3DIrisReplicationRoot* Root = Cast<UBox3DIrisReplicationRoot>(Context.Instance))
	{
		Root->ActivateInitialState();
		if (UWorld* World = Root->GetWorld())
		{
			if (UBox3DIrisReplicationSubsystem* Replication = World->GetSubsystem<UBox3DIrisReplicationSubsystem>())
			{
				Replication->AddClientRoot(Root);
			}
		}
	}
}

void UBox3DIrisNetObjectFactory::DetachedFromReplication(const FDestroyedContext& Context)
{
	if (UBox3DIrisReplicationRoot* Root = Cast<UBox3DIrisReplicationRoot>(Context.DestroyedInstance))
	{
		if (UWorld* World = Root->GetWorld())
		{
			if (UBox3DIrisReplicationSubsystem* Replication = World->GetSubsystem<UBox3DIrisReplicationSubsystem>())
			{
				Replication->RemoveClientRoot(Root);
			}
		}
		Root->DetachAndClear();
	}
}

TOptional<UNetObjectFactory::FWorldInfoData> UBox3DIrisNetObjectFactory::GetWorldInfo(
	const FWorldInfoContext& Context) const
{
	return NullOpt;
}

float UBox3DIrisNetObjectFactory::GetPollFrequency(UE::Net::FNetRefHandle RootObjectHandle,
	UObject* RootObjectInstance)
{
	return 0.0f;
}

TUniquePtr<UE::Net::FNetObjectCreationHeader> UBox3DIrisNetObjectFactory::CreateAndFillHeader(
	UE::Net::FNetRefHandle Handle)
{
	const UBox3DIrisReplicationRoot* Root = Cast<UBox3DIrisReplicationRoot>(Bridge->GetReplicatedObject(Handle));
	if (Root == nullptr) return nullptr;
	TUniquePtr<FBox3DIrisCreationHeader> Header = MakeUnique<FBox3DIrisCreationHeader>();
	Header->WorldEpoch = Root->GetWorldEpoch();
	Header->NetworkOrigin = Root->GetNetworkOrigin();
	Header->OriginRevision = Root->GetOriginRevision();
	return Header;
}

bool UBox3DIrisNetObjectFactory::SerializeHeader(const UE::Net::FCreationHeaderContext& Context,
	const UE::Net::FNetObjectCreationHeader* Header)
{
	const FBox3DIrisCreationHeader* CreationHeader = static_cast<const FBox3DIrisCreationHeader*>(Header);
	UE::Net::FNetBitStreamWriter* Writer = Context.Serialization.GetBitStreamWriter();
	if (CreationHeader == nullptr || Writer == nullptr || CreationHeader->Version != FactoryHeaderVersion) return false;
	uint64 Bits = 0;
	Writer->WriteBits(CreationHeader->Version, 8);
	Writer->WriteBits(CreationHeader->WorldEpoch.A, 32);
	Writer->WriteBits(CreationHeader->WorldEpoch.B, 32);
	Writer->WriteBits(CreationHeader->WorldEpoch.C, 32);
	Writer->WriteBits(CreationHeader->WorldEpoch.D, 32);
	FMemory::Memcpy(&Bits, &CreationHeader->NetworkOrigin.X, sizeof(Bits));
	WriteUInt64(*Writer, Bits);
	FMemory::Memcpy(&Bits, &CreationHeader->NetworkOrigin.Y, sizeof(Bits));
	WriteUInt64(*Writer, Bits);
	FMemory::Memcpy(&Bits, &CreationHeader->NetworkOrigin.Z, sizeof(Bits));
	WriteUInt64(*Writer, Bits);
	Writer->WriteBits(CreationHeader->OriginRevision, 32);
	return !Writer->IsOverflown();
}

TUniquePtr<UE::Net::FNetObjectCreationHeader> UBox3DIrisNetObjectFactory::CreateAndDeserializeHeader(
	const UE::Net::FCreationHeaderContext& Context)
{
	UE::Net::FNetBitStreamReader* Reader = Context.Serialization.GetBitStreamReader();
	if (Reader == nullptr) return nullptr;
	const uint32 Version = Reader->ReadBits(8);
	if (Reader->IsOverflown() || Version != FactoryHeaderVersion) return nullptr;
	TUniquePtr<FBox3DIrisCreationHeader> Header = MakeUnique<FBox3DIrisCreationHeader>();
	Header->Version = uint8(Version);
	Header->WorldEpoch.A = Reader->ReadBits(32);
	Header->WorldEpoch.B = Reader->ReadBits(32);
	Header->WorldEpoch.C = Reader->ReadBits(32);
	Header->WorldEpoch.D = Reader->ReadBits(32);
	uint64 Bits = ReadUInt64(*Reader);
	FMemory::Memcpy(&Header->NetworkOrigin.X, &Bits, sizeof(Bits));
	Bits = ReadUInt64(*Reader);
	FMemory::Memcpy(&Header->NetworkOrigin.Y, &Bits, sizeof(Bits));
	Bits = ReadUInt64(*Reader);
	FMemory::Memcpy(&Header->NetworkOrigin.Z, &Bits, sizeof(Bits));
	Header->OriginRevision = Reader->ReadBits(32);
	if (Reader->IsOverflown() || !FMath::IsFinite(Header->NetworkOrigin.X)
		|| !FMath::IsFinite(Header->NetworkOrigin.Y) || !FMath::IsFinite(Header->NetworkOrigin.Z)) return nullptr;
	return Header;
}
