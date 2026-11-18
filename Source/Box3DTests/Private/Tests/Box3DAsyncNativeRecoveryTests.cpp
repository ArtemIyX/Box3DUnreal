#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "box3d/box3d.h"
#include "box3d/collision.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncNativeSleepingRecovery,
	"Box3D.Async.NativeSleepingRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncNativeSleepingRecovery::RunTest(const FString& Parameters)
{
	b3WorldDef WorldDef = b3DefaultWorldDef();
	WorldDef.gravity = b3Vec3{0, 0, -10};
	WorldDef.workerCount = 1;
	const b3WorldId World = b3CreateWorld(&WorldDef);
	if (!TestTrue(TEXT("World valid"), b3World_IsValid(World))) return false;
	ON_SCOPE_EXIT { b3DestroyWorld(World); };
	b3BodyDef BodyDef = b3DefaultBodyDef();
	const b3BodyId Ground = b3CreateBody(World, &BodyDef);
	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	const b3BoxHull GroundHull = b3MakeBoxHull(5, 5, 0.5f);
	const b3ShapeId GroundShape = b3CreateHullShape(Ground, &ShapeDef, &GroundHull.base);
	BodyDef.type = b3_dynamicBody;
	BodyDef.position = b3Pos{0, 0, 2};
	const b3BodyId Body = b3CreateBody(World, &BodyDef);
	ShapeDef.enableContactEvents = true;
	const b3Sphere Sphere{{0, 0, 0}, 0.5f};
	const b3ShapeId SphereShape = b3CreateSphereShape(Body, &ShapeDef, &Sphere);
	bool bBegin = false;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		b3World_Step(World, 1.0f / 60.0f, 1);
		const b3ContactEvents Events = b3World_GetContactEvents(World);
		bBegin |= Events.beginCount > 0;
		if (bBegin && !b3Body_IsAwake(Body)) break;
	}
	TestTrue(TEXT("Native contact begin observed"), bBegin);
	TestFalse(TEXT("Body sleeps on the contact"), b3Body_IsAwake(Body));
	const int32 Capacity = b3Shape_GetContactCapacity(SphereShape);
	TArray<b3ContactData> Data;
	Data.SetNumUninitialized(Capacity);
	const int32 Count = Capacity ? b3Shape_GetContactData(SphereShape, Data.GetData(), Capacity) : 0;
	bool bFound = false;
	for (int32 I = 0; I < Count; ++I)
		bFound |= (B3_ID_EQUALS(Data[I].shapeIdA, GroundShape) && B3_ID_EQUALS(Data[I].shapeIdB, SphereShape))
			|| (B3_ID_EQUALS(Data[I].shapeIdB, GroundShape) && B3_ID_EQUALS(Data[I].shapeIdA, SphereShape));
	TestTrue(TEXT("Sleeping contact appears in recovery getter"), bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBox3DAsyncNativeCcdSensorRecovery,
	"Box3D.Async.NativeCcdSensorRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBox3DAsyncNativeCcdSensorRecovery::RunTest(const FString& Parameters)
{
	b3WorldDef WorldDef = b3DefaultWorldDef();
	WorldDef.gravity = b3Vec3{0, 0, 0};
	WorldDef.workerCount = 1;
	WorldDef.enableContinuous = true;
	const b3WorldId World = b3CreateWorld(&WorldDef);
	if (!TestTrue(TEXT("World valid"), b3World_IsValid(World))) return false;
	ON_SCOPE_EXIT { b3DestroyWorld(World); };
	b3BodyDef BodyDef = b3DefaultBodyDef();
	const b3BodyId SensorBody = b3CreateBody(World, &BodyDef);
	b3ShapeDef ShapeDef = b3DefaultShapeDef();
	ShapeDef.isSensor = true;
	ShapeDef.enableSensorEvents = true;
	const b3BoxHull SensorHull = b3MakeBoxHull(0.05f, 2, 2);
	const b3ShapeId Sensor = b3CreateHullShape(SensorBody, &ShapeDef, &SensorHull.base);
	BodyDef.type = b3_dynamicBody;
	BodyDef.position = b3Pos{-1, 0, 0};
	BodyDef.linearVelocity = b3Vec3{120, 0, 0};
	BodyDef.isBullet = true;
	const b3BodyId VisitorBody = b3CreateBody(World, &BodyDef);
	ShapeDef.isSensor = false;
	const b3Sphere Sphere{{0, 0, 0}, 0.1f};
	const b3ShapeId Visitor = b3CreateSphereShape(VisitorBody, &ShapeDef, &Sphere);
	bool bCrossingReported = false;
	bool bMatchingGetter = false;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		b3World_Step(World, 1.0f / 60.0f, 1);
		const b3SensorEvents Events = b3World_GetSensorEvents(World);
		for (int32 I = 0; I < Events.beginCount; ++I)
		{
			if (!B3_ID_EQUALS(Events.beginEvents[I].sensorShapeId, Sensor)
				|| !B3_ID_EQUALS(Events.beginEvents[I].visitorShapeId, Visitor)) continue;
			bCrossingReported = b3Body_GetPosition(VisitorBody).x > 0.15;
			const int32 Capacity = b3Shape_GetSensorCapacity(Sensor);
			TArray<b3ShapeId> Data;
			Data.SetNumUninitialized(Capacity);
			const int32 Count = Capacity ? b3Shape_GetSensorData(Sensor, Data.GetData(), Capacity) : 0;
			for (int32 J = 0; J < Count; ++J) bMatchingGetter |= B3_ID_EQUALS(Data[J], Visitor);
		}
	}
	TestTrue(TEXT("Fast crossing reported beyond the sensor"), bCrossingReported);
	TestTrue(TEXT("CCD visitor appears in recovery getter at enter step"), bMatchingGetter);
	return true;
}
#endif
