#include "DroneDiveComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/InputComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Math/UnrealMathUtility.h"

UDroneDiveComponent::UDroneDiveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bIsDiving = false;
	bIsRecoveringRotation = false;
	CurrentSpeed = 0.0f;
}

void UDroneDiveComponent::BeginPlay()
{
	Super::BeginPlay();
	

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (Owner)
	{
		OriginalNormalRot = Owner->GetActorRotation();
		OriginalNormalRot.Pitch = 0.0f;
		OriginalNormalRot.Roll = 0.0f;
	}
}

void UDroneDiveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsDiving)
		UpdateDive(DeltaTime);
	else if (bIsRecoveringRotation)
		UpdateRotationRecover(DeltaTime);
}

void UDroneDiveComponent::BindInput()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (PC && PC->InputComponent)
	{
		PC->InputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &UDroneDiveComponent::OnDiveKeyPressed);
	}
}

void UDroneDiveComponent::OnDiveKeyPressed()
{
	if (bIsDiving || bIsRecoveringRotation) return;

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (Owner)
	{
		OriginalNormalRot = Owner->GetActorRotation();
		OriginalNormalRot.Pitch = 0.0f;
		OriginalNormalRot.Roll = 0.0f;
	}

	StartDive(DiveTargetLocation);
}

void UDroneDiveComponent::StartDive(const FVector& TargetWorldLocation)
{
	if (bIsDiving) return;

	TargetLocation = TargetWorldLocation;
	bIsDiving = true;
	bIsRecoveringRotation = false;
	CurrentSpeed = 0.0f;

	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Green, TEXT("俯冲：加速→匀速→减速"));
}

void UDroneDiveComponent::StopDive()
{
	bIsDiving = false;
	bIsRecoveringRotation = true;
	CurrentSpeed = 0.0f;

	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Red, TEXT("俯冲结束，回正"));
}


void UDroneDiveComponent::UpdateDive(float DeltaTime)
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner) return;

	FVector SelfLoc = Owner->GetActorLocation();
	FVector V = TargetLocation - SelfLoc;
	float DistToTarget = V.Size();

	if (DistToTarget < DiveStopDist)
	{
		StopDive();
		return;
	}

	// ---------------------------
	// 俯仰：轨迹夹角 = 机身倾斜
	// ---------------------------
	FVector HorizDir = V;
	HorizDir.Z = 0.0f;
	HorizDir.Normalize();

	float H = HorizDir.Size();
	float Z = V.Z;
	float PitchAngle = 0.0f;

	if (!FMath::IsNearlyZero(H))
	{
		PitchAngle = FMath::Atan2(Z, H) * 180.0f / PI;
	}

	// 最终姿态：机头朝目标 + 俯仰向下
	FRotator TargetRot = HorizDir.Rotation();
	TargetRot.Pitch = PitchAngle;
	TargetRot.Roll = 0.0f;

	const float FixedDelta = 0.0167f;
	FRotator NewRot = FMath::RInterpTo(Owner->GetActorRotation(), TargetRot, FixedDelta, RotInterpSpeed);
	Owner->SetActorRotation(NewRot);

	// ---------------------------
	// 速度：加速 → 限最高 → 减速
	// ---------------------------
	if (DistToTarget > DecelerateDistance)
	{
		CurrentSpeed = FMath::Min(MaxSpeed, CurrentSpeed + Acceleration * DeltaTime);
	}
	else
	{
		CurrentSpeed = FMath::Max(0.0f, CurrentSpeed - Deceleration * DeltaTime);
	}

	FVector MoveDir = V.GetSafeNormal();
	FVector MoveOffset = MoveDir * CurrentSpeed * DeltaTime;
	Owner->AddActorWorldOffset(MoveOffset, true);
}

void UDroneDiveComponent::UpdateRotationRecover(float DeltaTime)
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner)
	{
		bIsRecoveringRotation = false;
		return;
	}

	FRotator CurrentRot = Owner->GetActorRotation();
	const float FixedDelta = 0.0167f;
	FRotator NewRot = FMath::RInterpTo(CurrentRot, OriginalNormalRot, FixedDelta, RecoverInterpSpeed);
	Owner->SetActorRotation(NewRot);

	if (FMath::Abs(CurrentRot.Pitch - OriginalNormalRot.Pitch) < RotErrorThresh &&
		FMath::Abs(CurrentRot.Yaw - OriginalNormalRot.Yaw) < RotErrorThresh &&
		FMath::Abs(CurrentRot.Roll - OriginalNormalRot.Roll) < RotErrorThresh)
	{
		bIsRecoveringRotation = false;
		Owner->SetActorRotation(OriginalNormalRot);

		if (GEngine)
			GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Blue, TEXT("已回正水平"));
	}
}