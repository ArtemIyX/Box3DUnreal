#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Box3DReplicationSettings.generated.h"

/** Distance keys are maximum distances in centimeters; values are update rates in Hz. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Box3D Replication"))
class BOX3DREPLICATION_API UBox3DReplicationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBox3DReplicationSettings();
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Maximum distance (cm) to update rate (Hz). The largest distance is the relevance cutoff. */
	UPROPERTY(EditAnywhere, config, Category = "Distance Tiers")
	TMap<float, float> DistanceRatesHz;

	/** Extra distance (cm) before changing an existing rate tier. */
	UPROPERTY(EditAnywhere, config, Category = "Distance Tiers", meta = (ClampMin = "0", Units = "cm"))
	float TierHysteresis = 100.0f;

	/** Extra distance (cm) before removing an already relevant body. */
	UPROPERTY(EditAnywhere, config, Category = "Distance Tiers", meta = (ClampMin = "0", Units = "cm"))
	float CullHysteresis = 250.0f;
};
