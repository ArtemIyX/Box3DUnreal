#pragma once

#include "CoreMinimal.h"

namespace Box3D::Fracture
{
	/// Inputs that fully determine a fracture layout (together with the proxy).
	struct FFractureParams
	{
		/// PRNG seed for site generation.
		int32 Seed = 0;

		/// Number of Voronoi sites to generate. The output fragment count matches
		/// unless degenerate cells collapse (rare) or MinFragmentVolume merging
		/// kicks in.
		int32 CellCount = 8;

		/// Impact location (cm) that site density biases toward when RadialBias > 0.
		FVector ImpactPoint = FVector::ZeroVector;

		/// Radius (cm) of the impact-biased site cluster. Clustered candidates are
		/// drawn uniformly inside this ball and rejected if outside the proxy, so a
		/// ball mostly outside the proxy yields fewer clustered sites.
		double ImpactRadius = 50.0;

		/// Fraction of sites drawn from the impact cluster: 0 = uniform site
		/// density, 1 = fully impact-clustered. Clamped to [0, 1].
		double RadialBias = 0.0;

		/// Fragments below this volume (cm^3) merge into the neighbor with the
		/// largest shared-face area (ties broken by lower fragment index), repeated
		/// deterministically until every remaining fragment with neighbors meets
		/// the threshold. Merged fragments are unions of convex cells (possibly
		/// non-convex); volumes add exactly, adjacency stays symmetric.
		double MinFragmentVolume = 0.0;

		/// Snap every site's coordinate on this proxy-local axis (0/1/2) to the
		/// proxy's bounds centre, so all bisector planes stand perpendicular to
		/// that axis and every cell is a prism spanning the proxy's full extent
		/// on it. For thin slabs (cladding, glass, panels) where layered
		/// front/back cells would jam behind each other. INDEX_NONE = free 3D sites.
		int32 FlattenAxis = INDEX_NONE;

		/// Explicit proxy-local sites. When non-empty they replace seeded
		/// generation (Seed, CellCount, RadialBias and ImpactRadius are ignored):
		/// each site is flattened, dropped when outside the proxy, quantized and
		/// deduplicated on the grid, in array order. Lets callers lay cells out
		/// deliberately (a jittered grid for cladding) on the same deterministic
		/// clipper.
		TArray<FVector> Sites;
	};
}
