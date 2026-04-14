#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "DroneDiveComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnable))
class UE5DRONECONTROL_API UDroneDiveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDroneDiveComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Drone Dive")
	void StartDive(const FVector& TargetWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Drone Dive")
	void StopDive();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Dive")
	FVector DiveTargetLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Dive|Speed")
	float MaxSpeed = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Dive|Speed")
	float Acceleration = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Dive|Speed")
	float Deceleration = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Dive|Speed")
	float DecelerateDistance = 600.0f;

private:
	void UpdateDive(float DeltaTime);
	void UpdateRotationRecover(float DeltaTime);
	void BindInput();
	void OnDiveKeyPressed();

	bool bIsDiving;
	bool bIsRecoveringRotation;
	FVector TargetLocation;
	FRotator OriginalNormalRot;
	float CurrentSpeed;

	const float RotInterpSpeed = 8.0f;
	const float RecoverInterpSpeed = 5.0f;
	const float DiveStopDist = 120.0f;
	const float RotErrorThresh = 1.0f;
};