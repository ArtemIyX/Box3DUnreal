#include "Components/Box3DAsyncRagdollComponent.h"
#include "Components/Box3DAsyncRagdollAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Math/RotationMatrix.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ReferenceSkeleton.h"
#include "World/Box3DAsyncRagdollSettings.h"
#include "World/Box3DAsyncRagdollSubsystem.h"

namespace
{
	FQuat AsyncRagdollFrameRotation(const FTransform& Frame)
	{
		const FMatrix M = FRotationMatrix::Make(Frame.GetRotation());
		return FMatrix(M.GetUnitAxis(EAxis::Y), M.GetUnitAxis(EAxis::Z), M.GetUnitAxis(EAxis::X), FVector::ZeroVector).ToQuat();
	}
	float AsyncSwingLimit(EAngularConstraintMotion Motion, float Limit)
	{
		return Motion == ACM_Free ? 180.0f : (Motion == ACM_Locked ? 0.0f : Limit);
	}
	bool CopyAsyncRagdollShapes(const USkeletalBodySetup& Setup, double Scale, float Friction, TArray<FBox3DAsyncBodyShapeDesc>& Out)
	{
		const FKAggregateGeom& G = Setup.AggGeom;
		if (!G.TaperedCapsuleElems.IsEmpty() || !G.LevelSetElems.IsEmpty() || !G.SkinnedLevelSetElems.IsEmpty()
			|| !G.MLLevelSetElems.IsEmpty() || !G.SkinnedTriangleMeshElems.IsEmpty()) return false;
		for (const FKSphereElem& E : G.SphereElems)
		{
			FBox3DAsyncBodyShapeDesc& S = Out.AddDefaulted_GetRef();
			S.Kind = EBox3DAsyncBodyShapeKind::Sphere; S.LocalTransform.SetLocation(FVector(E.Center) * Scale);
			S.Radius = float(E.Radius * Scale); S.Friction = Friction; S.Options.Density = 1000.0f;
		}
		for (const FKSphylElem& E : G.SphylElems)
		{
			FBox3DAsyncBodyShapeDesc& S = Out.AddDefaulted_GetRef();
			S.Kind = EBox3DAsyncBodyShapeKind::Capsule; S.LocalTransform = FTransform(E.Rotation.Quaternion(), FVector(E.Center) * Scale);
			S.Radius = float(E.Radius * Scale); S.HalfHeight = float((E.Length * 0.5 + E.Radius) * Scale);
			S.Friction = Friction; S.Options.Density = 1000.0f;
		}
		for (const FKBoxElem& E : G.BoxElems)
		{
			FBox3DAsyncBodyShapeDesc& S = Out.AddDefaulted_GetRef();
			S.Kind = EBox3DAsyncBodyShapeKind::Box; S.LocalTransform = FTransform(E.Rotation.Quaternion(), FVector(E.Center) * Scale);
			S.HalfExtents = FVector(E.X, E.Y, E.Z) * (0.5 * Scale); S.Friction = Friction; S.Options.Density = 1000.0f;
		}
		for (const FKConvexElem& E : G.ConvexElems)
		{
			if (E.VertexData.Num() < 4 || E.VertexData.Num() > 64) return false;
			FBox3DAsyncBodyShapeDesc& S = Out.AddDefaulted_GetRef();
			S.Kind = EBox3DAsyncBodyShapeKind::Convex; S.Friction = Friction; S.Options.Density = 1000.0f;
			for (const FVector& Vertex : E.VertexData) S.ConvexPoints.Add(E.GetTransform().TransformPosition(Vertex) * Scale);
		}
		return !Out.IsEmpty();
	}
}

UBox3DAsyncRagdollComponent::UBox3DAsyncRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

bool UBox3DAsyncRagdollComponent::StartRagdoll(USkeletalMeshComponent* Mesh, FVector InitialVelocity)
{
	check(IsInGameThread());
	if (Group.Id != 0 || !IsValid(Mesh) || !GetOwner() || Mesh->GetOwner() != GetOwner()
		|| !Mesh->GetSkeletalMeshAsset() || !Mesh->GetPhysicsAsset() || InitialVelocity.ContainsNaN()) return false;
	TArray<UBox3DAsyncRagdollComponent*> OtherRagdolls;
	GetOwner()->GetComponents(OtherRagdolls);
	for (const UBox3DAsyncRagdollComponent* Other : OtherRagdolls)
		if (Other && Other != this && Other->GetTargetMesh() == Mesh) return false;
	UBox3DAsyncRagdollSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UBox3DAsyncRagdollSubsystem>() : nullptr;
	if (!Subsystem || Mesh->GetWorld() != GetWorld()) return false;
	const FVector MeshScale = Mesh->GetComponentScale();
	if (MeshScale.ContainsNaN() || MeshScale.GetMin() <= 0.0 || !FMath::IsNearlyEqual(MeshScale.X, MeshScale.Y)
		|| !FMath::IsNearlyEqual(MeshScale.X, MeshScale.Z)) return false;
	const double Scale = MeshScale.X;
	const UBox3DAsyncRagdollSettings* Settings = GetDefault<UBox3DAsyncRagdollSettings>();
	if (!FMath::IsFinite(Settings->LinearDamping) || Settings->LinearDamping < 0.0f
		|| !FMath::IsFinite(Settings->AngularDamping) || Settings->AngularDamping < 0.0f
		|| !FMath::IsFinite(Settings->Friction) || Settings->Friction < 0.0f
		|| !FMath::IsFinite(Settings->MassScale) || Settings->MassScale <= 0.0f
		|| !FMath::IsFinite(Settings->MinBodyMassFraction) || Settings->MinBodyMassFraction < 0.0f || Settings->MinBodyMassFraction > 1.0f
		|| !FMath::IsFinite(Settings->LimitSlackDegrees) || Settings->LimitSlackDegrees < 0.0f
		|| !FMath::IsFinite(Settings->ConstraintHertz) || Settings->ConstraintHertz < 0.0f
		|| !FMath::IsFinite(Settings->ConstraintDampingRatio) || Settings->ConstraintDampingRatio < 0.0f) return false;
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
	FBox3DAsyncRagdollDesc Desc;
	TMap<FName, int32> BoneSlots;
	for (const USkeletalBodySetup* Setup : Asset->SkeletalBodySetups)
	{
		if (!Setup) continue;
		const int32 BoneIndex = Skeleton.FindBoneIndex(Setup->BoneName);
		if (BoneIndex == INDEX_NONE || BoneSlots.Contains(Setup->BoneName)) return false;
		FBox3DAsyncRagdollBoneDesc& Bone = Desc.Bones.AddDefaulted_GetRef();
		Bone.BoneName = Setup->BoneName; Bone.Body.Motion = EBox3DAsyncBodyMotion::Dynamic;
		Bone.Body.Transform = Mesh->GetBoneTransform(BoneIndex);
		if (!Bone.Body.Transform.GetScale3D().Equals(MeshScale, KINDA_SMALL_NUMBER)) return false;
		Bone.Body.Transform.SetScale3D(FVector::OneVector);
		Bone.Body.InitialVelocity = InitialVelocity; Bone.Body.LinearDamping = Settings->LinearDamping; Bone.Body.AngularDamping = Settings->AngularDamping;
		const FBodyInstance& Instance = Setup->DefaultInstance;
		const float Mass = Instance.bOverrideMass ? Instance.GetMassOverride() : Setup->CalculateMass(Mesh);
		Bone.Body.MassOverride = FMath::IsFinite(Mass) ? FMath::Max(0.0f, Mass * Settings->MassScale) : 0.0f;
		if (!CopyAsyncRagdollShapes(*Setup, Scale, Settings->Friction, Bone.Body.Shapes)) return false;
		BoneSlots.Add(Bone.BoneName, Desc.Bones.Num() - 1);
	}
	if (Desc.Bones.IsEmpty()) return false;
	float MaxMass = 0.0f;
	for (const FBox3DAsyncRagdollBoneDesc& Bone : Desc.Bones) MaxMass = FMath::Max(MaxMass, Bone.Body.MassOverride);
	const float MinMass = MaxMass * Settings->MinBodyMassFraction;
	for (FBox3DAsyncRagdollBoneDesc& Bone : Desc.Bones)
		if (Bone.Body.MassOverride > 0.0f) Bone.Body.MassOverride = FMath::Max(Bone.Body.MassOverride, MinMass);
	for (const UPhysicsConstraintTemplate* Template : Asset->ConstraintSetup)
	{
		if (!Template) continue;
		const FConstraintInstance& CI = Template->DefaultInstance;
		const int32* Child = BoneSlots.Find(CI.ConstraintBone1);
		const int32* Parent = BoneSlots.Find(CI.ConstraintBone2);
		if (!Child || !Parent) continue;
		const FTransform FrameA = CI.GetRefFrame(EConstraintFrame::Frame2);
		const FTransform FrameB = CI.GetRefFrame(EConstraintFrame::Frame1);
		FBox3DAsyncRagdollLinkDesc& Link = Desc.Links.AddDefaulted_GetRef();
		Link.Parent = *Parent; Link.Child = *Child;
		Link.Joint.ConstraintHertz = Settings->ConstraintHertz;
		Link.Joint.ConstraintDampingRatio = Settings->ConstraintDampingRatio;
		Link.Joint.LocalFrameA = FTransform(AsyncRagdollFrameRotation(FrameA), FrameA.GetTranslation() * Scale);
		Link.Joint.LocalFrameB = FTransform(AsyncRagdollFrameRotation(FrameB), FrameB.GetTranslation() * Scale);
		const EAngularConstraintMotion Swing1 = CI.GetAngularSwing1Motion(), Swing2 = CI.GetAngularSwing2Motion(), Twist = CI.GetAngularTwistMotion();
		const FQuat WorldA = Desc.Bones[*Parent].Body.Transform.GetRotation() * Link.Joint.LocalFrameA.GetRotation();
		const FQuat WorldB = Desc.Bones[*Child].Body.Transform.GetRotation() * Link.Joint.LocalFrameB.GetRotation();
		const FQuat Relative = WorldA.Inverse() * WorldB;
		if (Swing1 == ACM_Locked && Swing2 == ACM_Locked && Twist == ACM_Locked)
		{
			Link.Joint.Type = EBox3DAsyncJointType::Weld;
			Link.Joint.LocalFrameB.SetRotation(Desc.Bones[*Child].Body.Transform.GetRotation().Inverse() * WorldA);
			continue;
		}
		Link.Joint.Type = EBox3DAsyncJointType::Spherical;
		const float Slack = FMath::DegreesToRadians(FMath::Max(0.0f, Settings->LimitSlackDegrees));
		FQuat SwingQ, TwistQ; Relative.ToSwingTwist(FVector::ZAxisVector, SwingQ, TwistQ);
		const float SpawnSwing = 2.0f * FMath::Acos(FMath::Clamp(float(FMath::Abs(SwingQ.W)), 0.0f, 1.0f));
		const float SwingDegrees = FMath::Max(AsyncSwingLimit(Swing1, CI.GetAngularSwing1Limit()), AsyncSwingLimit(Swing2, CI.GetAngularSwing2Limit()));
		if (SwingDegrees < 179.0f)
		{
			Link.Joint.bEnableConeLimit = true;
			Link.Joint.ConeAngle = FMath::Clamp(FMath::Max(FMath::DegreesToRadians(SwingDegrees), SpawnSwing + Slack), 0.0f, 0.98f * PI);
		}
		if (Twist != ACM_Free)
		{
			Link.Joint.bEnableTwistLimit = true;
			const float Limit = Twist == ACM_Locked ? 0.0f : FMath::Clamp(FMath::DegreesToRadians(CI.GetAngularTwistLimit()), 0.0f, 0.98f * PI);
			const float Spawn = Relative.GetTwistAngle(FVector::ZAxisVector);
			Link.Joint.LowerTwist = FMath::Clamp(FMath::Min(-Limit, Spawn - Slack), -0.98f * PI, 0.0f);
			Link.Joint.UpperTwist = FMath::Clamp(FMath::Max(Limit, Spawn + Slack), 0.0f, 0.98f * PI);
		}
	}
	FBox3DAsyncRagdollHandle NewGroup;
	if (!Subsystem->TryCreateRagdoll(Desc, NewGroup)) return false;
	BaseLocalPose = Mesh->GetBoneSpaceTransforms();
	if (BaseLocalPose.Num() != Skeleton.GetNum()) BaseLocalPose = Skeleton.GetRefBonePose();
	BaseComponentPose.SetNum(Skeleton.GetNum());
	const TArray<FTransform>& ComponentSpace = Mesh->GetComponentSpaceTransforms();
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		BaseComponentPose[Index] = ComponentSpace.IsValidIndex(Index) ? ComponentSpace[Index]
			: (Parent != INDEX_NONE ? BaseLocalPose[Index] * BaseComponentPose[Parent] : BaseLocalPose[Index]);
	}
	Group = NewGroup; Ragdolls = Subsystem; TargetMesh = Mesh; TargetAsset = Mesh->GetSkeletalMeshAsset();
	LastStatus = EBox3DAsyncRagdollStatus::CreatingBodies; bStopRequested = false;
	SavedAnimClass = Mesh->GetAnimClass(); SavedAnimationMode = uint8(Mesh->GetAnimationMode());
	SavedVisibilityTick = uint8(Mesh->VisibilityBasedAnimTickOption);
	return true;
}

EBox3DAsyncRagdollStatus UBox3DAsyncRagdollComponent::GetRagdollStatus() const
{
	const UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get();
	return Subsystem && Group.Id != 0 ? Subsystem->GetRagdollStatus(Group) : LastStatus;
}

void UBox3DAsyncRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Group.Id == 0) return;
	USkeletalMeshComponent* Mesh = TargetMesh.Get();
	const EBox3DAsyncRagdollStatus Status = GetRagdollStatus();
	if (Status == EBox3DAsyncRagdollStatus::Failed || Status == EBox3DAsyncRagdollStatus::Removed
		|| Status == EBox3DAsyncRagdollStatus::Unavailable) { StopRagdoll(); return; }
	if (!Mesh || Mesh->GetSkeletalMeshAsset() != TargetAsset.Get()) { StopRagdoll(); return; }
	if (Status == EBox3DAsyncRagdollStatus::Active && !bPresentationInstalled && !bStopRequested)
	{
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetAnimInstanceClass(UBox3DAsyncRagdollAnimInstance::StaticClass());
		bPresentationInstalled = true;
	}
}

void UBox3DAsyncRagdollComponent::RestorePresentation()
{
	if (USkeletalMeshComponent* Mesh = TargetMesh.Get(); Mesh && bPresentationInstalled)
	{
		Mesh->VisibilityBasedAnimTickOption = static_cast<EVisibilityBasedAnimTickOption>(SavedVisibilityTick);
		if (Mesh->GetAnimClass() == UBox3DAsyncRagdollAnimInstance::StaticClass())
		{
			Mesh->SetAnimationMode(static_cast<EAnimationMode::Type>(SavedAnimationMode));
			if (SavedAnimationMode == uint8(EAnimationMode::AnimationBlueprint)) Mesh->SetAnimInstanceClass(SavedAnimClass);
		}
	}
	bPresentationInstalled = false;
}

void UBox3DAsyncRagdollComponent::StopRagdoll()
{
	UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get();
	const EBox3DAsyncRagdollStatus Status = GetRagdollStatus();
	RestorePresentation();
	if (Subsystem && Group.Id != 0 && Status != EBox3DAsyncRagdollStatus::Failed
		&& Status != EBox3DAsyncRagdollStatus::Removed && Status != EBox3DAsyncRagdollStatus::Unavailable)
	{
		bStopRequested = true;
		Subsystem->TryRemoveRagdoll(Group);
		return;
	}
	if (Subsystem && Group.Id != 0) Subsystem->ForgetRagdoll(Group);
	LastStatus = Status;
	Group = {}; Ragdolls.Reset(); TargetMesh.Reset(); TargetAsset.Reset(); SavedAnimClass = nullptr;
	BaseLocalPose.Reset(); BaseComponentPose.Reset(); bStopRequested = false;
}

void UBox3DAsyncRagdollComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RestorePresentation();
	if (UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get(); Subsystem && Group.Id != 0)
	{
		const EBox3DAsyncRagdollStatus Status = Subsystem->GetRagdollStatus(Group);
		if (Status == EBox3DAsyncRagdollStatus::Failed || Status == EBox3DAsyncRagdollStatus::Removed) Subsystem->ForgetRagdoll(Group);
		else Subsystem->TryRemoveRagdoll(Group, true);
	}
	Group = {};
	Ragdolls.Reset();
	TargetMesh.Reset();
	TargetAsset.Reset();
	Super::EndPlay(Reason);
}

bool UBox3DAsyncRagdollComponent::GetBoneState(FName Bone, FBox3DBPBodyState& OutState) const
{
	OutState = {};
	const UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get();
	FBox3DAsyncBodyState State;
	if (!Subsystem || !Subsystem->TryGetBoneState(Group, Bone, State)) return false;
	OutState.Body.World = State.World;
	FMemory::Memcpy(&OutState.Body.Id, &State.BodyId, sizeof(State.BodyId));
	FMemory::Memcpy(&OutState.CaptureSerial, &State.CaptureSerial, sizeof(State.CaptureSerial));
	FMemory::Memcpy(&OutState.SimulationStep, &State.SimulationStep, sizeof(State.SimulationStep));
	OutState.CaptureMonotonicSeconds = State.CaptureMonotonicSeconds;
	OutState.Transform = State.Transform;
	OutState.LinearVelocity = State.LinearVelocity;
	OutState.AngularVelocity = State.AngularVelocity;
	OutState.Mass = State.Mass;
	OutState.bAwake = State.bAwake;
	OutState.bEnabled = State.bEnabled;
	return true;
}

bool UBox3DAsyncRagdollComponent::AddImpulse(FVector Impulse)
{
	UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get();
	return Subsystem && Subsystem->TryApplyRagdollImpulse(Group, Impulse);
}

bool UBox3DAsyncRagdollComponent::AddImpulseAtBone(FName Bone, FVector Impulse, FVector WorldPoint)
{
	UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get();
	if (!Subsystem) return false;
	FBox3DAsyncBodyCommand Command;
	Command.Type = EBox3DAsyncBodyCommandType::ApplyImpulseAtPoint;
	Command.Value = Impulse; Command.Point = WorldPoint;
	return Subsystem->TrySubmitBoneCommand(Group, Bone, Command);
}

void UBox3DAsyncRagdollComponent::BuildLocalPose(TArray<FTransform>& OutLocalPose)
{
	check(IsInGameThread());
	USkeletalMeshComponent* Mesh = TargetMesh.Get();
	UBox3DAsyncRagdollSubsystem* Subsystem = Ragdolls.Get();
	FBox3DAsyncRagdollPose Pose;
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Subsystem || !Subsystem->TryGetLatestRagdollPose(Group, Pose)) return;
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	if (BaseLocalPose.Num() != Skeleton.GetNum() || BaseComponentPose.Num() != Skeleton.GetNum()) return;
	TMap<int32, int32> Slots;
	int32 Root = INDEX_NONE;
	for (int32 Slot = 0; Slot < Pose.BoneNames.Num(); ++Slot)
	{
		const int32 Index = Skeleton.FindBoneIndex(Pose.BoneNames[Slot]);
		if (Index != INDEX_NONE) { Slots.Add(Index, Slot); if (Root == INDEX_NONE || Index < Root) Root = Index; }
	}
	if (Root == INDEX_NONE) return;
	const FTransform& RootWorld = Pose.WorldTransforms[Slots.FindChecked(Root)];
	const FTransform ComponentWorld = Mesh->GetComponentTransform();
	const FVector NewLocation = RootWorld.GetLocation() - ComponentWorld.TransformVector(BaseComponentPose[Root].GetTranslation());
	if (!NewLocation.Equals(ComponentWorld.GetLocation(), 0.01)) Mesh->SetWorldLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	const FTransform WorldToComponent = Mesh->GetComponentTransform().Inverse();
	TArray<FTransform> Components;
	Components.SetNumUninitialized(Skeleton.GetNum());
	OutLocalPose.SetNumUninitialized(Skeleton.GetNum());
	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		FTransform CS;
		if (const int32* Slot = Slots.Find(Index))
		{
			CS = Pose.WorldTransforms[*Slot] * WorldToComponent;
			CS.SetScale3D(BaseComponentPose[Index].GetScale3D());
		}
		else CS = Parent != INDEX_NONE ? BaseLocalPose[Index] * Components[Parent] : BaseLocalPose[Index];
		Components[Index] = CS;
		OutLocalPose[Index] = Parent != INDEX_NONE ? CS.GetRelativeTransform(Components[Parent]) : CS;
	}
}
