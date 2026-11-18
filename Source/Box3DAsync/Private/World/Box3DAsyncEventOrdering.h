#pragma once

#include "World/Box3DAsyncSubsystem.h"

namespace Box3DAsyncPrivate
{
	struct FEventPairKey
	{
		uint64 A = 0;
		uint64 B = 0;
		bool bSensor = false;

		bool operator==(const FEventPairKey& Other) const
		{ return A == Other.A && B == Other.B && bSensor == Other.bSensor; }
		friend uint32 GetTypeHash(const FEventPairKey& Key)
		{ return HashCombine(HashCombine(::GetTypeHash(Key.A), ::GetTypeHash(Key.B)), ::GetTypeHash(Key.bSensor)); }
	};

	inline FEventPairKey ShapePair(const FBox3DAsyncEvent& Event)
	{
		return {Event.ShapeA, Event.ShapeB, Event.Kind == EBox3DAsyncEventKind::SensorEnter || Event.Kind == EBox3DAsyncEventKind::SensorExit};
	}

	inline FEventPairKey BodyPair(const FBox3DAsyncEvent& Event)
	{
		const bool bSensor = Event.Kind == EBox3DAsyncEventKind::SensorEnter || Event.Kind == EBox3DAsyncEventKind::SensorExit;
		return bSensor || Event.BodyA <= Event.BodyB ? FEventPairKey{Event.BodyA, Event.BodyB, bSensor}
			: FEventPairKey{Event.BodyB, Event.BodyA, false};
	}

	inline bool EventLess(const FBox3DAsyncEvent& A, const FBox3DAsyncEvent& B)
	{
		if (A.Kind != B.Kind) return uint8(A.Kind) < uint8(B.Kind);
		if (A.BodyA != B.BodyA) return A.BodyA < B.BodyA;
		if (A.BodyB != B.BodyB) return A.BodyB < B.BodyB;
		if (A.ShapeA != B.ShapeA) return A.ShapeA < B.ShapeA;
		if (A.ShapeB != B.ShapeB) return A.ShapeB < B.ShapeB;
		if (A.ContactWorld != B.ContactWorld) return A.ContactWorld < B.ContactWorld;
		if (A.ContactIndex != B.ContactIndex) return A.ContactIndex < B.ContactIndex;
		if (A.ContactGeneration != B.ContactGeneration) return A.ContactGeneration < B.ContactGeneration;
		if (A.Point.X != B.Point.X) return A.Point.X < B.Point.X;
		if (A.Point.Y != B.Point.Y) return A.Point.Y < B.Point.Y;
		if (A.Point.Z != B.Point.Z) return A.Point.Z < B.Point.Z;
		if (A.Normal.X != B.Normal.X) return A.Normal.X < B.Normal.X;
		if (A.Normal.Y != B.Normal.Y) return A.Normal.Y < B.Normal.Y;
		if (A.Normal.Z != B.Normal.Z) return A.Normal.Z < B.Normal.Z;
		if (A.ApproachSpeed != B.ApproachSpeed) return A.ApproachSpeed < B.ApproachSpeed;
		if (A.MaterialA != B.MaterialA) return A.MaterialA < B.MaterialA;
		return A.MaterialB < B.MaterialB;
	}

#if WITH_DEV_AUTOMATION_TESTS
	BOX3DASYNC_API void AggregateBodiesForTest(TArray<FBox3DAsyncEvent>& Active, FBox3DAsyncEventBatch& Batch, bool bBaseline);
#endif
}
