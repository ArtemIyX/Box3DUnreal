#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Box3DAsyncRagdollSettings.generated.h"

UCLASS(config=Game, defaultconfig, meta=(DisplayName="Box3D Async Ragdolls"))
class BOX3DASYNC_API UBox3DAsyncRagdollSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	UPROPERTY(EditAnywhere, config, Category="Limits", meta=(ClampMin="1", ClampMax="1024"))
	int32 MaxRagdolls = 64;
	UPROPERTY(EditAnywhere, config, Category="Limits", meta=(ClampMin="1", ClampMax="256"))
	int32 MaxBones = 64;
	UPROPERTY(EditAnywhere, config, Category="Limits", meta=(ClampMin="1", ClampMax="512"))
	int32 MaxJoints = 128;
	UPROPERTY(EditAnywhere, config, Category="Limits", meta=(ClampMin="1", ClampMax="4096"))
	int32 MaxShapes = 512;
	UPROPERTY(EditAnywhere, config, Category="Limits", meta=(ClampMin="4", ClampMax="262144"))
	int32 MaxConvexPoints = 32768;
	/** Time allowed for asynchronous group creation; failed groups are rolled back. */
	UPROPERTY(EditAnywhere, config, Category="Lifecycle", meta=(ClampMin="1", ClampMax="120"))
	float CreationTimeoutSeconds = 10.0f;
	UPROPERTY(EditAnywhere, config, Category="Bodies", meta=(ClampMin="0"))
	float LinearDamping = 0.05f;
	UPROPERTY(EditAnywhere, config, Category="Bodies", meta=(ClampMin="0"))
	float AngularDamping = 1.0f;
	UPROPERTY(EditAnywhere, config, Category="Bodies", meta=(ClampMin="0"))
	float Friction = 0.7f;
	UPROPERTY(EditAnywhere, config, Category="Bodies", meta=(ClampMin="0.01"))
	float MassScale = 1.0f;
	/** Clamp bone masses to this fraction of the largest asset mass. Zero disables the clamp. */
	UPROPERTY(EditAnywhere, config, Category="Bodies", meta=(ClampMin="0", ClampMax="1"))
	float MinBodyMassFraction = 0.05f;
	UPROPERTY(EditAnywhere, config, Category="Constraints", meta=(ClampMin="0", Units="deg"))
	float LimitSlackDegrees = 3.0f;
	UPROPERTY(EditAnywhere, config, Category="Constraints", meta=(ClampMin="0"))
	float ConstraintHertz = 0.0f;
	UPROPERTY(EditAnywhere, config, Category="Constraints", meta=(ClampMin="0"))
	float ConstraintDampingRatio = 2.0f;
};
