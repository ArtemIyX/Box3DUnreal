#pragma once

#include "CoreMinimal.h"

/// Result of one rate-independent elastic-perfectly-plastic hinge update.
enum class EBox3DPlasticHingeTransition : uint8
{
	None,
	Yielded,
	Broke,
};
