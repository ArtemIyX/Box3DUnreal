#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/Box3DAsyncJointType.h"
#include "Data/Structs/Box3DBPIdentity.h"
#include "Box3DBPJointDesc.generated.h"

/** Frames are body-local in cm; angles and angular speeds are radians. Forces use N and torques N*m. */
USTRUCT(BlueprintType)
struct BOX3DASYNC_API FBox3DBPJointDesc
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	EBox3DAsyncJointType Type = EBox3DAsyncJointType::Weld;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FBox3DBPIdentity BodyA;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FBox3DBPIdentity BodyB;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FTransform LocalFrameA = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FTransform LocalFrameB = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bCollideConnected = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float BreakForce = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float BreakTorque = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float ConstraintHertz = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float ConstraintDampingRatio = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableSpring = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float SpringHertz = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float SpringDampingRatio = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float LowerLimit = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float UpperLimit = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableMotor = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MotorSpeed = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MaxMotorForce = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MaxMotorTorque = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float DistanceLength = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float Target = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float AngularHertz = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float AngularDampingRatio = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector LinearMotorVelocity = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FVector AngularMotorVelocity = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MaxSpringForce = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MaxSpringTorque = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	FRotator TargetRotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableSteering = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float SteeringHertz = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float SteeringDampingRatio = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float TargetSteeringAngle = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float MaxSteeringTorque = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableSteeringLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float LowerSteeringLimit = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float UpperSteeringLimit = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableConeLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float ConeAngle = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	bool bEnableTwistLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float LowerTwist = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Box3D")
	float UpperTwist = 0.0f;
};
