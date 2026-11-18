#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Data/Enums/Box3DChannel.h"
#include "Data/Structs/Box3DFilter.h"
#include "Data/Structs/Box3DQueryFilter.h"
#include "Data/Structs/Box3DHitResult.h"

class AActor;
class UBox3DBodyComponent;









namespace Box3D
{
	/// int32 bitmask (UE editor-facing) -> 64-bit box3d bits. -1 means "all bits",
	/// including the upper 32 box3d bits UE never names.
	inline uint64 ToB3Bits(int32 Bits)
	{
		return Bits == -1 ? UINT64_MAX : static_cast<uint64>(static_cast<uint32>(Bits));
	}

	/// Resolve a shape hit back to the owning component/actor via body userData.
	BOX3DRUNTIME_API UBox3DBodyComponent* ResolveComponent(b3ShapeId ShapeId);

	/// A fresh negative b3Filter group index. Give one to every shape of a
	/// multi-body assembly (rope, cloth, ragdoll) to disable self-collision while
	/// separate assemblies still collide with each other.
	BOX3DRUNTIME_API int32 AllocateSelfCollisionGroup();
}
