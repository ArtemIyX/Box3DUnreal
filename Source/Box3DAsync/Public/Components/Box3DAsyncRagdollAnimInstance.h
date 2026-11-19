#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Box3DAsyncRagdollAnimInstance.generated.h"

/** Uses only a game-thread copy of a completed async pose in animation evaluation. */
UCLASS(NotBlueprintable, Transient)
class BOX3DASYNC_API UBox3DAsyncRagdollAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
