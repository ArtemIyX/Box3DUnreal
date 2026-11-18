#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/Box3DVertexDent.h"

/// Deterministic cosmetic deformation helpers. Geometry stays in UE centimetres;
/// callers own rendering and deliberately do not rebuild collision.
namespace Box3D::Deform
{


	/// Apply one dent in fixed vertex-index order. Vertices inside RadiusCm move
	/// inward, and no vertex moves farther than MaxDepthCm for this stamp. Returns
	/// the number of displaced vertices.
	BOX3DRUNTIME_API int32 ApplyVertexDent(TArray<FVector>& Vertices,
		const FBox3DVertexDent& Dent);

	/// Stable digest over vertices quantized to 0.001 cm in array order.
	BOX3DRUNTIME_API uint32 VertexHash(TConstArrayView<FVector> Vertices);
}
