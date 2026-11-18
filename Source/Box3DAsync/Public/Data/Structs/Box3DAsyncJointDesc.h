#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncJointType.h"
#include "Data/Structs/Box3DAsyncBodyHandle.h"

/** Joint frames are local to each body's origin, with positions in cm and unit scale.
 * Linear values are cm and cm/s; angular values are radians and rad/s.
 * Maximum forces are N and torques are N*m, matching the sync joint API. */
struct BOX3DASYNC_API FBox3DAsyncJointDesc
{
	EBox3DAsyncJointType Type = EBox3DAsyncJointType::Weld;
	FBox3DAsyncBodyHandle BodyA;
	FBox3DAsyncBodyHandle BodyB;
	FTransform LocalFrameA = FTransform::Identity;
	FTransform LocalFrameB = FTransform::Identity;
	bool bCollideConnected = false;
	/** Zero disables the corresponding automatic break threshold. */
	float BreakForce = 0.0f;
	float BreakTorque = 0.0f;
	float ConstraintHertz = 0.0f;
	float ConstraintDampingRatio = 0.0f;
	bool bEnableSpring = false;
	float SpringHertz = 0.0f;
	float SpringDampingRatio = 0.0f;
	bool bEnableLimit = false;
	float LowerLimit = 0.0f;
	float UpperLimit = 0.0f;
	bool bEnableMotor = false;
	float MotorSpeed = 0.0f;
	float MaxMotorForce = 0.0f;
	float MaxMotorTorque = 0.0f;
	float DistanceLength = 100.0f;
	float Target = 0.0f;
	float AngularHertz = 0.0f;
	float AngularDampingRatio = 0.0f;
	FVector LinearMotorVelocity = FVector::ZeroVector;
	FVector AngularMotorVelocity = FVector::ZeroVector;
	float MaxSpringForce = 0.0f;
	float MaxSpringTorque = 0.0f;
	FQuat TargetRotation = FQuat::Identity;
	bool bEnableSteering = false;
	float SteeringHertz = 0.0f;
	float SteeringDampingRatio = 0.0f;
	float TargetSteeringAngle = 0.0f;
	float MaxSteeringTorque = 0.0f;
	bool bEnableSteeringLimit = false;
	float LowerSteeringLimit = 0.0f;
	float UpperSteeringLimit = 0.0f;
	bool bEnableConeLimit = false;
	float ConeAngle = 3.0f;
	bool bEnableTwistLimit = false;
	float LowerTwist = 0.0f;
	float UpperTwist = 0.0f;
};
