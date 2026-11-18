#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/FractureParams.h"
#include "Data/Structs/Box3DFragmentFace.h"
#include "Data/Structs/Box3DFragmentNeighbor.h"
#include "Data/Structs/Box3DFragmentData.h"
#include "Data/Structs/FractureProxy.h"

/// Deterministic Voronoi fracture core — pure geometry, no actors, no rendering.
///
/// Fragments are convex: Voronoi cells of seeded sites clipped against a convex
/// proxy polytope by half-space bisector planes (see docs/DESTRUCTION_PLAN.md § D1
/// and docs/DESTRUCTION_RESEARCH.md § 3a). Fracture is a pure function of
/// (proxy, params): sites and bisector-plane offsets are quantized to a fixed grid
/// before clipping, iteration order is fixed, and bisector planes are canonical
/// per site pair, so the same inputs produce hash-identical fragments on every
/// run.
///
/// Everything here operates in UE centimeters. The cm -> m conversion happens at
/// the existing Box3DConversion seam when fragments are handed to box3d
/// (b3CreateHull) by later milestones.
namespace Box3D::Fracture
{
	/// Fixed quantization grid (cm) applied to site coordinates and bisector plane
	/// offsets before clipping, and to fragment data inside FractureLayoutHash.
	inline constexpr double QuantizeStep = 0.01;

	/// Snap a scalar to the QuantizeStep grid.
	FORCEINLINE double Quantize(double Value)
	{
		return FMath::RoundToDouble(Value / QuantizeStep) * QuantizeStep;
	}

	FORCEINLINE FVector Quantize(const FVector& V)
	{
		return FVector(Quantize(V.X), Quantize(V.Y), Quantize(V.Z));
	}











	/// Axis-aligned box proxy — the common case for D1 tests and debug commands.
	BOX3DRUNTIME_API FFractureProxy MakeBoxProxy(const FVector& Center, const FVector& HalfExtents);

	/// Fracture Proxy into convex Voronoi fragments. Deterministic: same
	/// (proxy, params) produce bit-identical output. Returns false when inputs are
	/// degenerate (empty proxy, CellCount < 1) or no fragment survives.
	BOX3DRUNTIME_API bool Fracture(const FFractureProxy& Proxy, const FFractureParams& Params,
		TArray<FBox3DFragmentData>& OutFragments);

	/// djb2 digest (b3Hash) over quantized fragment data, folded in stable order —
	/// same layout hashes identically across runs and machines. Mirrors the
	/// Box3DSnapshot.h hashing pattern.
	BOX3DRUNTIME_API uint32 FractureLayoutHash(const TArray<FBox3DFragmentData>& Fragments);
}
