#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Box3DAsyncSettings.generated.h"

/** Async worker queue limits. Changes apply to newly initialized worlds. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Box3D Async"))
class BOX3DASYNC_API UBox3DAsyncSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Maximum queued worker mutations per world. */
	UPROPERTY(EditAnywhere, config, Category = "Queues", meta = (ClampMin = "1", ClampMax = "256"))
	int32 CommandQueueCapacity = 256;

	/** Publish latest body states for game-thread readback. */
	UPROPERTY(EditAnywhere, config, Category = "State Readback")
	bool bPublishBodyStates = true;

	/** Publish joint force and torque samples after worker steps. */
	UPROPERTY(EditAnywhere, config, Category = "State Readback")
	bool bPublishJointStates = true;

	/** Maximum live persistent fields per world. */
	UPROPERTY(EditAnywhere, config, Category = "Fields", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 MaxPersistentFields = 64;

	/** Maximum magnitude of a field force on one body in kg*cm/s^2. */
	UPROPERTY(EditAnywhere, config, Category = "Fields", meta = (ClampMin = "1", ClampMax = "1000000000000"))
	float MaxFieldForce = 1.0e12f;

	/** Maximum outstanding joint identities, including failed joints awaiting removal. */
	UPROPERTY(EditAnywhere, config, Category = "Joints", meta = (ClampMin = "1", ClampMax = "65536"))
	int32 MaxJoints = 4096;

	/** Buffered joint break events before the raw stream reports a sticky gap. */
	UPROPERTY(EditAnywhere, config, Category = "Joints", meta = (ClampMin = "1", ClampMax = "65536"))
	int32 MaxJointBreakEvents = 1024;
};
