// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsGameMode.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/SimpleCoordinateService.h"
#include "DroneOps/Core/CesiumCoordinateService.h"
#include "DroneOpsPlayerController.h"
#include "MultiDroneCharacter.h"
#include "EngineUtils.h"

ADroneOpsGameMode::ADroneOpsGameMode()
{
	// Set default player controller class
	PlayerControllerClass = ADroneOpsPlayerController::StaticClass();

	// Do not auto-spawn a default pawn. We only use pawns already placed in the level.
	DefaultPawnClass = nullptr;
}

void ADroneOpsGameMode::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: BeginPlay"));

	// Initialize coordinate service
	InitializeCoordinateService();

	// Initialize drone registry
	InitializeDroneRegistry();

	RemainingPossessRetries = 10;
	RetryPossessPlacedPawns();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RetryPossessTimerHandle,
			this,
			&ADroneOpsGameMode::RetryPossessPlacedPawns,
			0.1f,
			true);
	}
}

void ADroneOpsGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	PossessPlacedPawn(NewPlayer, true, false);
}

APawn* ADroneOpsGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	return nullptr;
}

void ADroneOpsGameMode::InitializeCoordinateService()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DroneOpsGameMode: No GameInstance found"));
		return;
	}

	UDroneRegistrySubsystem* Registry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry)
	{
		UE_LOG(LogTemp, Error, TEXT("DroneOpsGameMode: DroneRegistrySubsystem not found"));
		return;
	}

	// Choose coordinate service based on bUseCesiumCoordinates
	if (bUseCesiumCoordinates)
	{
		UCesiumCoordinateService* CoordService = NewObject<UCesiumCoordinateService>(this);
		CoordService->Initialize(GetWorld());
		Registry->SetCoordinateService(CoordService);
		UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: CesiumCoordinateService initialized"));
	}
	else
	{
		USimpleCoordinateService* CoordService = NewObject<USimpleCoordinateService>(this);
		if (CoordService)
		{
			Registry->SetCoordinateService(CoordService);
			UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: SimpleCoordinateService initialized"));
		}
	}
}

void ADroneOpsGameMode::InitializeDroneRegistry()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UDroneRegistrySubsystem* Registry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry)
	{
		return;
	}

	// Registry is ready for drone registration
	// Drones will be registered by MultiDroneManager or individual actors
	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: DroneRegistry ready for registration"));
}

APawn* ADroneOpsGameMode::FindUnpossessedPlacedPawn(bool bLogDiscoveredPawns) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	int32 DiscoveredPawnCount = 0;
	for (TActorIterator<AMultiDroneCharacter> It(World); It; ++It)
	{
		AMultiDroneCharacter* DronePawn = *It;
		if (!IsValid(DronePawn) || DronePawn->IsPendingKillPending())
		{
			continue;
		}

		++DiscoveredPawnCount;
		if (bLogDiscoveredPawns)
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("DroneOpsGameMode: Found placed pawn %s (Controller=%s)"),
				*DronePawn->GetName(),
				DronePawn->Controller ? *DronePawn->Controller->GetName() : TEXT("None"));
		}

		if (DronePawn->Controller == nullptr)
		{
			return DronePawn;
		}
	}

	if (bLogDiscoveredPawns)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Found %d AMultiDroneCharacter actors, none unpossessed"), DiscoveredPawnCount);
	}

	return nullptr;
}

void ADroneOpsGameMode::PossessPlacedPawn(APlayerController* PlayerController, bool bSilentIfNotFound, bool bLogDiscoveredPawns)
{
	if (!PlayerController)
	{
		return;
	}

	if (PlayerController->GetPawn())
	{
		return;
	}

	if (APawn* ExistingPawn = FindUnpossessedPlacedPawn(bLogDiscoveredPawns))
	{
		PlayerController->Possess(ExistingPawn);
		UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Possessed placed pawn %s"), *ExistingPawn->GetName());
		return;
	}

	if (!bSilentIfNotFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: No unpossessed placed AMultiDroneCharacter found for %s"), *PlayerController->GetName());
	}
}

void ADroneOpsGameMode::RetryPossessPlacedPawns()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bAnyPawnPossessedThisPass = false;
	bool bAnyControllerMissingPawn = false;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!PlayerController)
		{
			continue;
		}

		if (PlayerController->GetPawn())
		{
			continue;
		}

		bAnyControllerMissingPawn = true;
		APawn* PawnBefore = PlayerController->GetPawn();
		PossessPlacedPawn(PlayerController, RemainingPossessRetries > 1, true);
		if (!PawnBefore && PlayerController->GetPawn())
		{
			bAnyPawnPossessedThisPass = true;
		}
	}

	--RemainingPossessRetries;

	if (!bAnyControllerMissingPawn || RemainingPossessRetries <= 0 || !World->GetTimerManager().IsTimerActive(RetryPossessTimerHandle))
	{
		World->GetTimerManager().ClearTimer(RetryPossessTimerHandle);
		return;
	}

	if (bAnyPawnPossessedThisPass)
	{
		RemainingPossessRetries = FMath::Max(RemainingPossessRetries, 3);
	}
}
