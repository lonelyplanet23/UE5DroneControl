// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiDronePlayerController.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UE5DroneControl.h"
#include "UE5DroneControlCharacter.h"
#include "MultiDroneManager.h"

AMultiDronePlayerController::AMultiDronePlayerController()
{
	CachedManager = nullptr;
}

void AMultiDronePlayerController::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> FoundManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMultiDroneManager::StaticClass(), FoundManagers);
	if (FoundManagers.Num() > 0)
	{
		CachedManager = Cast<AMultiDroneManager>(FoundManagers[0]);
	}
}

void AMultiDronePlayerController::SetupInputComponent()
{
	// Avoid Super to prevent binding to the base OnSetDestinationReleased.
	APlayerController::SetupInputComponent();

	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}

		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
		{
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Started, this, &AMultiDronePlayerController::OnInputStarted);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Triggered, this, &AMultiDronePlayerController::OnSetDestinationTriggered);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Completed, this, &AMultiDronePlayerController::OnSetDestinationReleased);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Canceled, this, &AMultiDronePlayerController::OnSetDestinationReleased);

			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Started, this, &AMultiDronePlayerController::OnInputStarted);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Triggered, this, &AMultiDronePlayerController::OnTouchTriggered);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Completed, this, &AMultiDronePlayerController::OnTouchReleased);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Canceled, this, &AMultiDronePlayerController::OnTouchReleased);
		}
		else
		{
			UE_LOG(LogUE5DroneControl, Error, TEXT("'%s' Enhanced Input Component not found"), *GetNameSafe(this));
		}

		// Legacy input bindings for number keys (ensure mappings exist in Project Settings)
		InputComponent->BindAction("SwitchToTopDown", IE_Pressed, this, &AMultiDronePlayerController::SwitchToTopDownCharacter);
		InputComponent->BindAction("SwitchToDrone1", IE_Pressed, this, &AMultiDronePlayerController::SwitchToDrone1);
		InputComponent->BindAction("SwitchToDrone2", IE_Pressed, this, &AMultiDronePlayerController::SwitchToDrone2);
		InputComponent->BindAction("SwitchToDrone3", IE_Pressed, this, &AMultiDronePlayerController::SwitchToDrone3);
	}
}

void AMultiDronePlayerController::OnInputStarted()
{
	StopMovement();
	UpdateCachedDestination();
}

void AMultiDronePlayerController::OnSetDestinationTriggered()
{
	FollowTime += GetWorld()->GetDeltaSeconds();
	UpdateCachedDestination();

	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn != nullptr)
	{
		const FVector WorldDirection = (CachedDestination - ControlledPawn->GetActorLocation()).GetSafeNormal();
		ControlledPawn->AddMovementInput(WorldDirection, 1.0f, false);
	}
}

void AMultiDronePlayerController::OnSetDestinationReleased()
{
	if (FollowTime <= ShortPressThreshold)
	{
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, CachedDestination);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FXCursor, CachedDestination, FRotator::ZeroRotator, FVector(1.f, 1.f, 1.f), true, true, ENCPoolMethod::None, true);
	}

	AUE5DroneControlCharacter* Sender = nullptr;
	if (CachedManager)
	{
		Sender = CachedManager->GetActiveSender();
	}
	if (!Sender)
	{
		Sender = Cast<AUE5DroneControlCharacter>(GetPawn());
	}

	if (Sender)
	{
		Sender->SetClickTargetLocation(CachedDestination, 1);
	}

	FollowTime = 0.f;
}

void AMultiDronePlayerController::OnTouchTriggered()
{
	bIsTouch = true;
	OnSetDestinationTriggered();
}

void AMultiDronePlayerController::OnTouchReleased()
{
	bIsTouch = false;
	OnSetDestinationReleased();
}

void AMultiDronePlayerController::UpdateCachedDestination()
{
	FHitResult Hit;
	bool bHitSuccessful = false;
	if (bIsTouch)
	{
		bHitSuccessful = GetHitResultUnderFinger(ETouchIndex::Touch1, ECollisionChannel::ECC_Visibility, true, Hit);
	}
	else
	{
		bHitSuccessful = GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, true, Hit);
	}

	if (bHitSuccessful)
	{
		CachedDestination = Hit.Location;
	}
}

void AMultiDronePlayerController::SwitchToTopDownCharacter()
{
	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn)
	{
		SetViewTargetWithBlend(ControlledPawn, 0.5f);
	}

	if (CachedManager)
	{
		CachedManager->ActiveDroneIndex = -1;
	}
}

void AMultiDronePlayerController::SwitchToDrone1()
{
	SwitchToDroneIndex(0);
}

void AMultiDronePlayerController::SwitchToDrone2()
{
	SwitchToDroneIndex(1);
}

void AMultiDronePlayerController::SwitchToDrone3()
{
	SwitchToDroneIndex(2);
}

void AMultiDronePlayerController::SwitchToDroneIndex(int32 Index)
{
	if (!CachedManager)
	{
		TArray<AActor*> FoundManagers;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMultiDroneManager::StaticClass(), FoundManagers);
		if (FoundManagers.Num() > 0)
		{
			CachedManager = Cast<AMultiDroneManager>(FoundManagers[0]);
		}
	}

	if (CachedManager)
	{
		CachedManager->SwitchToDrone(Index);
	}
	else
	{
		UE_LOG(LogUE5DroneControl, Warning, TEXT("MultiDroneManager not found"));
	}
}
