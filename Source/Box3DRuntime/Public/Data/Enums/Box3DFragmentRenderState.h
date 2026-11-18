#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DFragmentRenderState.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/// How ABox3DFracturedActor currently draws one fragment.
UENUM(BlueprintType)
enum class EBox3DFragmentRenderState : uint8
{
	/// Not drawn: non-Body tier, or destroyed.
	None,
	/// Baked into the actor's shared attached mesh (one static mesh for every
	/// fragment still held by the assembly; exterior and interior faces are its
	/// two sections). Rebuilt only when the attached set changes.
	Attached,
	/// A free body: drawn by its own static mesh component, moved by transform
	/// from the fragment body each tick.
	Loose,
};
