#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DPlasticHingeTransition.h"

/// Pure-data constitutive state for an elastic-perfectly-plastic hinge.
/// Angles are degrees and torque/stiffness are N*m and N*m/degree. When the
/// elastic offset exceeds YieldTorque / ElasticStiffness, the excess becomes a
/// permanent rest-angle increment. Break is measured from the initial rest.
struct BOX3DRUNTIME_API FBox3DPlasticHingeResponse
{
	float InitialRestAngleDegrees = 0.0f;
	float RestAngleDegrees = 0.0f;
	float YieldTorqueNm = 100.0f;
	float ElasticStiffnessNmPerDegree = 20.0f;
	float BreakAngleDegrees = 45.0f;
	bool bBroken = false;

	/// Advance the constitutive state at a measured hinge angle. The returned
	/// plastic increment is exact for the ideal elastic-perfectly-plastic model.
	EBox3DPlasticHingeTransition Advance(float CurrentAngleDegrees,
		float& OutPlasticIncrementDegrees, float& OutRestoringTorqueNm);
};
