#include "World/Box3DAsyncRagdollSubsystem.h"

#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "Subsystems/SubsystemCollection.h"
#include "World/Box3DAsyncRagdollSettings.h"

void UBox3DAsyncRagdollSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UBox3DAsyncSubsystem>();
	Physics = GetWorld() ? GetWorld()->GetSubsystem<UBox3DAsyncSubsystem>() : nullptr;
	const UBox3DAsyncRagdollSettings* Settings = GetDefault<UBox3DAsyncRagdollSettings>();
	MaxGroups = FMath::Clamp(Settings->MaxRagdolls, 1, 1024);
	MaxBones = FMath::Clamp(Settings->MaxBones, 1, 256);
	MaxJoints = FMath::Clamp(Settings->MaxJoints, 1, 512);
	MaxShapes = FMath::Clamp(Settings->MaxShapes, 1, 4096);
	MaxConvexPoints = FMath::Clamp(Settings->MaxConvexPoints, 4, 262144);
	CreationTimeout = FMath::IsFinite(Settings->CreationTimeoutSeconds)
		? FMath::Clamp(double(Settings->CreationTimeoutSeconds), 1.0, 120.0) : 10.0;
}

void UBox3DAsyncRagdollSubsystem::Deinitialize()
{
	if (UBox3DAsyncSubsystem* Subsystem = Physics.Get())
		for (TPair<uint64, FGroup>& Pair : Groups) Subsystem->UnregisterCreationConsumer(Pair.Value.Consumer);
	Groups.Reset();
	Physics.Reset();
	Super::Deinitialize();
}

TStatId UBox3DAsyncRagdollSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBox3DAsyncRagdollSubsystem, STATGROUP_Tickables);
}

bool UBox3DAsyncRagdollSubsystem::TryCreateRagdoll(const FBox3DAsyncRagdollDesc& Desc,
	FBox3DAsyncRagdollHandle& OutRagdoll)
{
	check(IsInGameThread());
	OutRagdoll = {};
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	if (!Subsystem || NextId == 0 || (Desc.bDisableSelfCollision && NextCollisionGroup == MIN_int32) || Groups.Num() >= MaxGroups || Desc.Bones.IsEmpty()
		|| Desc.Bones.Num() > MaxBones || Desc.Links.Num() > MaxJoints) return false;
	TSet<FName> Names;
	int64 TotalShapes = 0;
	int64 TotalPoints = 0;
	for (const FBox3DAsyncRagdollBoneDesc& Bone : Desc.Bones)
	{
		if (Bone.BoneName.IsNone() || Names.Contains(Bone.BoneName)
			|| Bone.Body.Motion != EBox3DAsyncBodyMotion::Dynamic || Bone.Body.Shapes.IsEmpty()) return false;
		Names.Add(Bone.BoneName);
		TotalShapes += Bone.Body.Shapes.Num();
		for (const FBox3DAsyncBodyShapeDesc& Shape : Bone.Body.Shapes) TotalPoints += Shape.ConvexPoints.Num();
		if (TotalShapes > MaxShapes || TotalPoints > MaxConvexPoints) return false;
	}
	for (const FBox3DAsyncRagdollLinkDesc& Link : Desc.Links)
		if (!Desc.Bones.IsValidIndex(Link.Parent) || !Desc.Bones.IsValidIndex(Link.Child) || Link.Parent == Link.Child) return false;
	FBox3DAsyncCreationConsumer Consumer;
	if (!Subsystem->TryRegisterCreationConsumer(Consumer)) return false;
	if (!WorldId.IsValid()) WorldId = Consumer.World;
	if (WorldId != Consumer.World) { Subsystem->UnregisterCreationConsumer(Consumer); return false; }
	const uint64 Id = NextId++;
	FGroup& Group = Groups.Add(Id);
	Group.Desc = Desc;
	for (FBox3DAsyncRagdollBoneDesc& Bone : Group.Desc.Bones) Bone.Body.bStartEnabled = false;
	if (Desc.bDisableSelfCollision)
	{
		const int32 CollisionGroup = NextCollisionGroup--;
		for (FBox3DAsyncRagdollBoneDesc& Bone : Group.Desc.Bones)
			for (FBox3DAsyncBodyShapeDesc& Shape : Bone.Body.Shapes) Shape.Options.GroupIndex = CollisionGroup;
	}
	Group.Consumer = Consumer;
	Group.StartedSeconds = FPlatformTime::Seconds();
	OutRagdoll = {WorldId, Id};
	return true;
}

EBox3DAsyncRagdollStatus UBox3DAsyncRagdollSubsystem::GetRagdollStatus(FBox3DAsyncRagdollHandle Ragdoll) const
{
	check(IsInGameThread());
	if (Ragdoll.World != WorldId || Ragdoll.Id == 0) return EBox3DAsyncRagdollStatus::Unavailable;
	const FGroup* Group = Groups.Find(Ragdoll.Id);
	return Group ? Group->Status : EBox3DAsyncRagdollStatus::Unavailable;
}

bool UBox3DAsyncRagdollSubsystem::TryRemoveRagdoll(FBox3DAsyncRagdollHandle Ragdoll, bool bForgetWhenRemoved)
{
	check(IsInGameThread());
	if (Ragdoll.World != WorldId || Ragdoll.Id == 0) return false;
	FGroup* Group = Groups.Find(Ragdoll.Id);
	if (!Group || Group->Status == EBox3DAsyncRagdollStatus::Removed || Group->Status == EBox3DAsyncRagdollStatus::Failed) return false;
	if (Group->Status == EBox3DAsyncRagdollStatus::Removing)
	{
		Group->bForgetWhenRemoved |= bForgetWhenRemoved;
		return true;
	}
	Group->Status = EBox3DAsyncRagdollStatus::Removing;
	Group->bForgetWhenRemoved = bForgetWhenRemoved;
	return true;
}

bool UBox3DAsyncRagdollSubsystem::ForgetRagdoll(FBox3DAsyncRagdollHandle Ragdoll)
{
	check(IsInGameThread());
	if (Ragdoll.World != WorldId || Ragdoll.Id == 0) return false;
	const FGroup* Group = Groups.Find(Ragdoll.Id);
	if (!Group || (Group->Status != EBox3DAsyncRagdollStatus::Removed && Group->Status != EBox3DAsyncRagdollStatus::Failed)) return false;
	Groups.Remove(Ragdoll.Id);
	return true;
}

void UBox3DAsyncRagdollSubsystem::Tick(float DeltaTime)
{
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	if (!Subsystem) return;
	for (auto It = Groups.CreateIterator(); It; ++It)
	{
		FGroup& Group = It.Value();
		if ((Group.Status == EBox3DAsyncRagdollStatus::CreatingBodies || Group.Status == EBox3DAsyncRagdollStatus::CreatingJoints)
			&& FPlatformTime::Seconds() - Group.StartedSeconds > CreationTimeout)
		{
			Group.bFailure = true;
			Group.Status = EBox3DAsyncRagdollStatus::Removing;
		}
		for (int32 Index = 0; Index < Group.Bodies.Num(); ++Index)
		{
			if (Group.BodyResults[Index] != 0) continue;
			FBox3DAsyncCreationResult Result;
			if (!Subsystem->TryTakeCreationResult(Group.Consumer, Group.Bodies[Index], Result)) continue;
			Group.BodyResults[Index] = Result.bSucceeded ? 1 : 2;
			if (!Result.bSucceeded) { Group.bFailure = true; Group.Status = EBox3DAsyncRagdollStatus::Removing; }
		}
		if (Group.Status == EBox3DAsyncRagdollStatus::CreatingBodies)
		{
			while (Group.NextBody < Group.Desc.Bones.Num())
			{
				FBox3DAsyncBodyHandle Body;
				if (!Subsystem->TryCreateBody(Group.Consumer, Group.Desc.Bones[Group.NextBody].Body, Body)) break;
				Group.Bodies.Add(Body);
				Group.BodyResults.Add(0);
				Group.BodyRemovals.Add(0);
				++Group.NextBody;
			}
			if (Group.NextBody == Group.Desc.Bones.Num() && !Group.BodyResults.Contains(0))
				Group.Status = EBox3DAsyncRagdollStatus::CreatingJoints;
		}
		if (Group.Status == EBox3DAsyncRagdollStatus::CreatingJoints)
		{
			while (Group.NextJoint < Group.Desc.Links.Num())
			{
				const FBox3DAsyncRagdollLinkDesc& Link = Group.Desc.Links[Group.NextJoint];
				FBox3DAsyncJointDesc JointDesc = Link.Joint;
				JointDesc.BodyA = Group.Bodies[Link.Parent];
				JointDesc.BodyB = Group.Bodies[Link.Child];
				FBox3DAsyncJointHandle Joint;
				if (!Subsystem->TryCreateJoint(JointDesc, Joint)) break;
				Group.Joints.Add(Joint);
				++Group.NextJoint;
			}
			bool bAllActive = Group.NextJoint == Group.Desc.Links.Num();
			for (const FBox3DAsyncJointHandle Joint : Group.Joints)
			{
				const EBox3DAsyncJointStatus Status = Subsystem->GetJointStatus(Joint);
				if (Status == EBox3DAsyncJointStatus::Failed || Status == EBox3DAsyncJointStatus::Broken
					|| Status == EBox3DAsyncJointStatus::Unavailable)
				{
					Group.bFailure = true;
					Group.Status = EBox3DAsyncRagdollStatus::Removing;
				}
				if (Status != EBox3DAsyncJointStatus::Active) bAllActive = false;
			}
			if (bAllActive && Group.Status == EBox3DAsyncRagdollStatus::CreatingJoints
				&& Subsystem->TrySetBodiesEnabled(Group.Bodies, true))
				Group.Status = EBox3DAsyncRagdollStatus::Active;
		}
		if (Group.Status != EBox3DAsyncRagdollStatus::Removing) continue;
		bool bJointsRemoved = true;
		for (const FBox3DAsyncJointHandle Joint : Group.Joints)
		{
			const EBox3DAsyncJointStatus Status = Subsystem->GetJointStatus(Joint);
			if (Status == EBox3DAsyncJointStatus::Unavailable) continue;
			bJointsRemoved = false;
			if (Status != EBox3DAsyncJointStatus::RemovalQueued) Subsystem->TryRemoveJoint(Joint);
		}
		if (!bJointsRemoved) continue;
		bool bBodiesRemoved = true;
		for (int32 Index = 0; Index < Group.Bodies.Num(); ++Index)
		{
			if (Group.BodyResults[Index] == 0) { bBodiesRemoved = false; continue; }
			if (Group.BodyResults[Index] == 2 || Group.BodyRemovals[Index] != 0) continue;
			if (Subsystem->TryRemoveBody(Group.Bodies[Index])) Group.BodyRemovals[Index] = 1;
			else bBodiesRemoved = false;
		}
		if (bBodiesRemoved)
		{
			Subsystem->UnregisterCreationConsumer(Group.Consumer);
			Group.Consumer = {};
			Group.Status = Group.bFailure ? EBox3DAsyncRagdollStatus::Failed : EBox3DAsyncRagdollStatus::Removed;
			if (Group.bForgetWhenRemoved) It.RemoveCurrent();
		}
	}
}

bool UBox3DAsyncRagdollSubsystem::TryGetLatestRagdollPose(FBox3DAsyncRagdollHandle Ragdoll,
	FBox3DAsyncRagdollPose& OutPose) const
{
	check(IsInGameThread());
	OutPose = {};
	const FGroup* Group = Ragdoll.World == WorldId ? Groups.Find(Ragdoll.Id) : nullptr;
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	FBox3DAsyncPoseFrame Frame;
	if (!Group || Group->Status != EBox3DAsyncRagdollStatus::Active || !Subsystem || !Subsystem->TryGetLatestPoseFrame(Frame)) return false;
	FBox3DAsyncRagdollPose Pose;
	Pose.World = WorldId;
	Pose.RagdollId = Ragdoll.Id;
	Pose.CaptureSerial = Frame.CaptureSerial;
	Pose.SimulationStep = Frame.SimulationSample.Step;
	for (int32 Index = 0; Index < Group->Bodies.Num(); ++Index)
	{
		const FBox3DAsyncPose* BodyPose = Frame.Poses.FindByPredicate([Id = Group->Bodies[Index].Id](const FBox3DAsyncPose& P) { return P.BodyId == Id; });
		if (!BodyPose) return false;
		Pose.BoneNames.Add(Group->Desc.Bones[Index].BoneName);
		Pose.WorldTransforms.Add(BodyPose->Transform);
	}
	OutPose = MoveTemp(Pose);
	return true;
}

bool UBox3DAsyncRagdollSubsystem::TryGetBoneState(FBox3DAsyncRagdollHandle Ragdoll, FName Bone,
	FBox3DAsyncBodyState& OutState) const
{
	check(IsInGameThread());
	OutState = {};
	const FGroup* Group = Ragdoll.World == WorldId ? Groups.Find(Ragdoll.Id) : nullptr;
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	if (!Group || Group->Status != EBox3DAsyncRagdollStatus::Active || !Subsystem) return false;
	const int32 Index = Group->Desc.Bones.IndexOfByPredicate([Bone](const FBox3DAsyncRagdollBoneDesc& Entry) { return Entry.BoneName == Bone; });
	return Group->Bodies.IsValidIndex(Index) && Subsystem->TryGetLatestBodyState(Group->Bodies[Index], OutState);
}

bool UBox3DAsyncRagdollSubsystem::TryApplyRagdollImpulse(FBox3DAsyncRagdollHandle Ragdoll, FVector Impulse)
{
	check(IsInGameThread());
	const FGroup* Group = Ragdoll.World == WorldId ? Groups.Find(Ragdoll.Id) : nullptr;
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	return Group && Group->Status == EBox3DAsyncRagdollStatus::Active && Subsystem
		&& Subsystem->TryApplyGroupImpulse(Group->Bodies, Impulse);
}

bool UBox3DAsyncRagdollSubsystem::TrySubmitBoneCommand(FBox3DAsyncRagdollHandle Ragdoll, FName Bone,
	const FBox3DAsyncBodyCommand& Command)
{
	check(IsInGameThread());
	const FGroup* Group = Ragdoll.World == WorldId ? Groups.Find(Ragdoll.Id) : nullptr;
	UBox3DAsyncSubsystem* Subsystem = Physics.Get();
	if (!Group || Group->Status != EBox3DAsyncRagdollStatus::Active || !Subsystem) return false;
	const int32 Index = Group->Desc.Bones.IndexOfByPredicate([Bone](const FBox3DAsyncRagdollBoneDesc& Entry) { return Entry.BoneName == Bone; });
	return Group->Bodies.IsValidIndex(Index) && Subsystem->TrySubmitBodyCommand(Group->Bodies[Index], Command);
}
