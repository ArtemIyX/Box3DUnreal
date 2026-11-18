#pragma once

#include "CoreMinimal.h"
#include "Box3DEnergyToCellCurve.generated.h"

/// Analytic impact-energy (J) -> Voronoi cell-count mapping: zero below
/// MinEnergy (the impact is absorbed), MinCellCount at MinEnergy, easing up to
/// MaxCellCount at FullEnergy with a sqrt curve (early joules buy cells fast,
/// later ones saturate), clamped above. Monotonic by construction.
USTRUCT(BlueprintType)
struct FBox3DEnergyToCellCurve
{
	GENERATED_BODY()

	/// Impacts below this energy (J) do not fracture at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float MinEnergy = 100.0f;

	/// Energy (J) at which the curve reaches MaxCellCount; higher energy clamps.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "0"))
	float FullEnergy = 20000.0f;

	/// Cell count of the weakest fracturing impact.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "1"))
	int32 MinCellCount = 4;

	/// Cell count cap (also the per-event cell cap of the D3 budget policy).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box3D|Destruction", meta = (ClampMin = "1"))
	int32 MaxCellCount = 48;
};
