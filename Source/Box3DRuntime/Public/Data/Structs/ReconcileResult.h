#pragma once

#include "CoreMinimal.h"
#include "box3d/box3d.h"

namespace Box3D
{
	/// Outcome of a reconcile, for logging / smoothing decisions.
	struct FReconcileResult
	{
		bool bCorrected = false;    // the world was rolled back and replayed
		int32 ReplayedFrames = 0;   // how many frames were re-stepped
		double MaxCorrection = 0.0; // largest per-body position change (meters)
	};
}
