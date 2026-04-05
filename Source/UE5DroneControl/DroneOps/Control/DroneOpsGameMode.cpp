// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsGameMode.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/SimpleCoordinateService.h"
#include "DroneOpsPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ADroneOpsGameMode::ADroneOpsGameMode()
{
	// Set default player controller class
	PlayerControllerClass = ADroneOpsPlayerController::StaticClass();

	// Load the TopDown character Blueprint so we get the spring-arm + camera setup
	static ConstructorHelpers::FClassFinder<APawn> TopDownPawnBP(
		TEXT("/Game/TopDown/Blueprints/BP_TopDownCharacter"));
	if (TopDownPawnBP.Succeeded())
	{
		DefaultPawnClass = TopDownPawnBP.Class;
	}
}

void ADroneOpsGameMode::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: BeginPlay"));

	// Initialize coordinate service
	InitializeCoordinateService();

	// Initialize drone registry
	InitializeDroneRegistry();
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

	// For now, always use SimpleCoordinateService (Cesium will be added later)
	USimpleCoordinateService* CoordService = NewObject<USimpleCoordinateService>(this);
	if (CoordService)
	{
		Registry->SetCoordinateService(CoordService);
		UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: SimpleCoordinateService initialized"));
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
