#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DTierThresholds.h"
#include "Box3DDestructionEventParams.generated.h"

/// Replicated numeric policy accompanying one destruction decision. Presentation
/// assets stay on the local destructible component; every value that can change
/// fragment geometry, tiering, bodies, or structural state travels in the event.
USTRUCT(BlueprintType)
struct FBox3DDestructionEventParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "1"))
	int32 CellCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	double ImpactRadius = 50.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0", ClampMax = "1"))
	double RadialBias = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	double MinFragmentVolume = 0.0;

	/// Proxy-local axis (0/1/2) whose site coordinate is flattened to the centre so
	/// cells span the full thickness (thin slabs); -1 keeps free 3D sites.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "-1", ClampMax = "2"))
	int32 FlattenAxis = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float MaterialToughness = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "1"))
	float FragmentDensity = 400.0f;

	/// Physics-hull inset toward each centroid (cm) so neighbouring hulls never overlap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float HullInsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	bool bStartAsleep = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	FBox3DTierThresholds Tiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float DebrisSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	bool bStructural = false;

	/// Structural only: every fragment is an anchor instead of auto-detecting
	/// support from surrounding static bodies.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction")
	bool bAnchorAllFragments = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float TensionStrengthPa = 1.0e6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float CompressionStrengthPa = 5.0e6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float ShearStrengthPa = 1.0e6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float SustainedOverloadHealthPerSecond = 1.0f;
};
