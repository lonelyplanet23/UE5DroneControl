// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiDroneManager.h"
#include "Kismet/GameplayStatics.h"
#include "RealTimeDroneReceiver.h"
#include "UE5DroneControlCharacter.h"
#include "UE5DroneControl.h"

AMultiDroneManager::AMultiDroneManager()
{
	PrimaryActorTick.bCanEverTick = false;
	ActiveDroneIndex = 0;
}

void AMultiDroneManager::BeginPlay()
{
	Super::BeginPlay();
	RefreshDroneLists();

	if (DroneReceivers.Num() == 0)
	{
		UE_LOG(LogUE5DroneControl, Warning, TEXT("MultiDroneManager: No RealTimeDroneReceiver found in level"));
		ActiveDroneIndex = -1;
		return;
	}

	if (ActiveDroneIndex < 0 || ActiveDroneIndex >= DroneReceivers.Num())
	{
		ActiveDroneIndex = 0;
	}
}

void AMultiDroneManager::RefreshDroneLists()
{
	DroneReceivers.Empty();
	DroneSenders.Empty();

	TArray<AActor*> ReceiverActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARealTimeDroneReceiver::StaticClass(), ReceiverActors);
	for (AActor* Actor : ReceiverActors)
	{
		if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(Actor))
		{
			DroneReceivers.Add(Receiver);
		}
	}

	DroneReceivers.Sort([](const ARealTimeDroneReceiver& A, const ARealTimeDroneReceiver& B)
	{
		return A.ListenPort < B.ListenPort;
	});

	TArray<AActor*> SenderActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AUE5DroneControlCharacter::StaticClass(), SenderActors);
	for (AActor* Actor : SenderActors)
	{
		AUE5DroneControlCharacter* Character = Cast<AUE5DroneControlCharacter>(Actor);
		if (!Character)
		{
			continue;
		}

		// Exclude RealTimeDroneReceiver instances from sender list.
		if (Character->IsA(ARealTimeDroneReceiver::StaticClass()))
		{
			continue;
		}

		DroneSenders.Add(Character);
	}
}

void AMultiDroneManager::SwitchToDrone(int32 Index)
{
	if (DroneReceivers.Num() == 0)
	{
		UE_LOG(LogUE5DroneControl, Warning, TEXT("MultiDroneManager: No receivers available"));
		return;
	}

	if (Index < 0 || Index >= DroneReceivers.Num())
	{
		UE_LOG(LogUE5DroneControl, Warning, TEXT("MultiDroneManager: Invalid drone index %d"), Index);
		return;
	}

	ARealTimeDroneReceiver* Target = DroneReceivers[Index];
	if (!Target)
	{
		UE_LOG(LogUE5DroneControl, Warning, TEXT("MultiDroneManager: Receiver at index %d is null"), Index);
		return;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PC->SetViewTargetWithBlend(Target, 0.5f);
	}

	ActiveDroneIndex = Index;
}

AUE5DroneControlCharacter* AMultiDroneManager::GetActiveSender() const
{
	if (ActiveDroneIndex < 0 || ActiveDroneIndex >= DroneSenders.Num())
	{
		return nullptr;
	}
	return DroneSenders[ActiveDroneIndex];
}

ARealTimeDroneReceiver* AMultiDroneManager::GetActiveReceiver() const
{
	if (ActiveDroneIndex < 0 || ActiveDroneIndex >= DroneReceivers.Num())
	{
		return nullptr;
	}
	return DroneReceivers[ActiveDroneIndex];
}
