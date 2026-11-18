#pragma once

#include "CoreMinimal.h"
#include "Box3DDestructionEventResult.generated.h"

/// Result of applying a destruction event locally. Hashes use int64 only because
/// Blueprint has no uint32 pin; values are always in the uint32 range.
USTRUCT(BlueprintType)
struct FBox3DDestructionEventResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Destruction")
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Destruction")
	bool bVisualOnly = false;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Destruction")
	bool bLayoutHashValidated = false;

	/// True when game code must request the authority's fragment/body correction.
	/// Predicted fragment bodies can use Box3D::ReconcileAndReplay for that state.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Destruction")
	bool bNeedsServerCorrection = false;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Destruction")
	int64 FractureLayoutHash = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D|Destruction")
	int64 BondHealthHash = 0;
};
