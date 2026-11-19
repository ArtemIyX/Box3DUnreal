#include "Components/Box3DAsyncRagdollAnimInstance.h"
#include "Components/Box3DAsyncRagdollComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "GameFramework/Actor.h"

namespace
{
	struct FAsyncRagdollAnimProxy final : public FAnimInstanceProxy
	{
		explicit FAsyncRagdollAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			const AActor* Owner = Instance->GetOwningActor();
			TArray<UBox3DAsyncRagdollComponent*> Components;
			if (Owner) Owner->GetComponents(Components);
			for (UBox3DAsyncRagdollComponent* Ragdoll : Components)
			{
				if (!IsValid(Ragdoll) || Ragdoll->GetTargetMesh() != Instance->GetOwningComponent()) continue;
				TArray<FTransform> NewPose;
				Ragdoll->BuildLocalPose(NewPose);
				if (!NewPose.IsEmpty()) LocalPose = MoveTemp(NewPose);
				break;
			}
		}
		virtual bool Evaluate(FPoseContext& Output) override
		{
			if (LocalPose.IsEmpty()) { Output.ResetToRefPose(); return true; }
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			for (FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
			{
				const int32 MeshIndex = Bones.MakeMeshPoseIndex(Index).GetInt();
				if (LocalPose.IsValidIndex(MeshIndex)) Output.Pose[Index] = LocalPose[MeshIndex];
			}
			Output.Pose.NormalizeRotations();
			return true;
		}
		TArray<FTransform> LocalPose;
	};
}

FAnimInstanceProxy* UBox3DAsyncRagdollAnimInstance::CreateAnimInstanceProxy()
{
	return new FAsyncRagdollAnimProxy(this);
}
