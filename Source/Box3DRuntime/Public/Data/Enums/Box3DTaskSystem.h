#pragma once

#include "CoreMinimal.h"
#include "Box3DTaskSystem.generated.h"

class UBox3DCollisionData;

/// Which scheduler runs box3d's solver tasks when WorkerCount > 1.
UENUM()
enum class EBox3DTaskSystem : uint8
{
	/// UE::Tasks — solver tasks run on the engine's shared worker threads.
	UnrealTasks,
	/// box3d's built-in scheduler — dedicated threads owned by the physics world.
	Box3DInternal,
};
