#pragma once

#include "CoreMinimal.h"
#include "Box3DTierThresholds.generated.h"

/// Fragment-volume thresholds (cm^3) routing fragments into tiers. Defaults of
/// zero keep every fragment in the Body tier. Dust wins where the thresholds
/// overlap: a fragment below RenderVolumeThreshold is dropped even when
/// RenderVolumeThreshold exceeds PhysicsVolumeThreshold.
USTRUCT(BlueprintType)
struct FBox3DTierThresholds
{
	GENERATED_BODY()

	/// Fragments below this volume (cm^3) skip physics bodies and welds and are
	/// routed to the debris burst.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	double PhysicsVolumeThreshold = 0.0;

	/// Fragments below this volume (cm^3) are dropped entirely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	double RenderVolumeThreshold = 0.0;
};
