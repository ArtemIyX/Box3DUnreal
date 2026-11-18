#pragma once

#include "CoreMinimal.h"
#include "box3d/id.h"
#include "Box3DWorldStats.generated.h"

class ABox3DFracturedActor;
class UBox3DBodyComponent;
class UBox3DDestructibleComponent;
class UBox3DJointComponent;
class UBox3DPlasticHingeComponent;
class FBox3DBakedScene;
class FBox3DUETaskPool;
class FBox3DDebugDrawer;
class FBox3DStaticSceneMirror;
struct b3Recording;

/// Snapshot of box3d's per-step profile and simulation counters, readable from
/// Blueprint. Times describe the most recent fixed step, in milliseconds. The
/// same data feeds `stat box3d`.
USTRUCT(BlueprintType)
struct FBox3DWorldStats
{
	GENERATED_BODY()

	/// Whole b3World_Step, ms.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	float StepMs = 0.0f;

	/// Broad-phase pair generation, ms.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	float PairsMs = 0.0f;

	/// Narrow-phase collision, ms.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	float CollideMs = 0.0f;

	/// Constraint solver, ms.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	float SolveMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 BodyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 ShapeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 ContactCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 JointCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 IslandCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 AwakeBodyCount = 0;

	/// Solver tasks spawned during the last step (0 when single-threaded).
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int32 TaskCount = 0;

	/// Bytes of memory held by the physics world.
	UPROPERTY(BlueprintReadOnly, Category = "Box3D")
	int64 MemoryBytes = 0;
};
