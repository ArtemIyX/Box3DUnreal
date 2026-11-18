#pragma once

#include "CoreMinimal.h"
#include "box3d/box3d.h"

namespace Box3D
{
	/// One dynamic body's restorable state, in box3d space.
	struct FBodyState
	{
		b3WorldTransform Transform{};
		b3Vec3 LinearVelocity{};
		b3Vec3 AngularVelocity{};
		bool bAwake = false;
	};
}
