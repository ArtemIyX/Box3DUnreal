#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/FractureParams.h"
#include "Data/Structs/Box3DTierThresholds.h"

class UMaterialInterface;
class UNiagaraSystem;

/// Inputs for Box3D::FractureMesh. Unlike the pure-geometry fracture core,
/// Fracture.ImpactPoint here is in WORLD space (cm) — FractureMesh converts it
/// into the source component's space before fracturing.
struct FBox3DFractureMeshParams
{
	Box3D::Fracture::FFractureParams Fracture;

	/// Material for interior (fracture-cut) faces — typically triplanar so the
	/// arbitrary cut geometry needs no UV authoring. Null falls back to the
	/// source component's material.
	UMaterialInterface* CoreMaterial = nullptr;

	/// Weld strength per unit of shared-face area (N/cm^2): each weld's break
	/// force is SharedFaceArea x MaterialToughness. <= 0 makes welds unbreakable.
	float MaterialToughness = 50.0f;

	/// Fragment density in kg/m^3.
	float FragmentDensity = 400.0f;

	/// Pull every fragment hull vertex this far (cm) toward its centroid before
	/// cooking, so neighbouring hulls never overlap. Coincident Voronoi faces
	/// otherwise leave sub-millimetre penetrations whose push-out friction can
	/// pin a fragment detached from a static assembly between its neighbours.
	/// Rendering is unaffected; 0 keeps exact hulls.
	float HullInsetCm = 0.0f;

	/// Spawn the fragment bodies asleep (welded rubble at rest). Defaults off
	/// because fracture usually follows an impact that should scatter the pieces.
	bool bStartAsleep = false;

	/// Volume thresholds routing fragments into Body / Debris / Dust tiers.
	/// Defaults keep every fragment in the Body tier (the D2 behavior).
	FBox3DTierThresholds Tiers;

	/// Radial scatter speed (cm/s) recorded for Debris-tier burst fragments.
	float DebrisSpeed = 300.0f;

	/// Niagara system spawned with the Debris-tier burst arrays (user parameters
	/// DebrisPositions / DebrisVelocities / DebrisSizes, world space). Null skips
	/// the spawn — the burst arrays stay inspectable data either way.
	UNiagaraSystem* DebrisSystem = nullptr;

	/// Anchored structural assembly (D4): fragments spawn as STATIC bodies, a
	/// bond graph tracks connectivity, and auto-detected anchors (chunks touching
	/// static world geometry) hold the assembly up. When a chunk is destroyed or
	/// a weld snaps, islands with no path to an anchor promote static->dynamic
	/// under the per-tick budget and fall as welded clumps. Assemblies with no
	/// anchors at build fall back to plain dynamic rubble (the default behavior).
	bool bStructural = false;

	/// Structural assemblies only: every fragment that gets a body is an anchor,
	/// skipping DetectAnchors. For cladding glued to an immovable surface (a
	/// destructible skin over a real wall) this makes support independent of
	/// what happens to sit behind the mesh; fragments then leave the assembly
	/// only through DetachFragment / DestroyFragment.
	bool bAnchorAllFragments = false;

	/// Sustained-load capacities in pascals. Non-positive disables that mode.
	float TensionStrengthPa = 1.0e6f;
	float CompressionStrengthPa = 5.0e6f;
	float ShearStrengthPa = 1.0e6f;

	/// Health removed per second for each unit of overload above capacity.
	float SustainedOverloadHealthPerSecond = 1.0f;

	/// Render flags for every fragment primitive (the attached mesh and each
	/// loose chip). Cladding over real geometry usually turns all three off:
	/// the wall behind it already shadows, feeds Lumen and takes decals.
	bool bCastShadow = true;
	bool bAffectIndirectLighting = true;
	bool bReceivesDecals = true;
};
