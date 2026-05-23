// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsGameMode.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/SimpleCoordinateService.h"
#include "DroneOps/Core/CesiumCoordinateService.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOpsPlayerController.h"
#include "MultiDroneCharacter.h"
#include "RealTimeDroneReceiver.h"
#include "CesiumGeoreference.h"
#include "EngineUtils.h"

ADroneOpsGameMode::ADroneOpsGameMode()
{
	PlayerControllerClass = ADroneOpsPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	ReceiverDroneClass = ARealTimeDroneReceiver::StaticClass();
	ShadowDroneClass = AMultiDroneCharacter::StaticClass();
}

void ADroneOpsGameMode::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	// Load BP_DroneOpsPlayerController before the PC is spawned,
	// so blueprint-configured widget classes (DroneInfoPanelWidgetClass etc.) take effect.
	TSoftClassPtr<ADroneOpsPlayerController> SoftPC(FSoftObjectPath(
		TEXT("/Game/DroneOps/Blueprints/BP_DroneOpsPlayerController.BP_DroneOpsPlayerController_C")));
	if (TSubclassOf<APlayerController> LoadedPC = SoftPC.LoadSynchronous())
	{
		PlayerControllerClass = LoadedPC;
		UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Loaded BP_DroneOpsPlayerController"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: BP_DroneOpsPlayerController not found, using C++ class"));
	}
}

void ADroneOpsGameMode::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: BeginPlay"));

	// Always (re)load the blueprint receiver class at BeginPlay.
	// Do not rely on the CDO value — it may be stale from a previous cook.
	{
		TSoftClassPtr<ARealTimeDroneReceiver> SoftClass(FSoftObjectPath(TEXT("/Game/DroneOps/Blueprints/BP_RealTimeDrone.BP_RealTimeDrone_C")));
		TSubclassOf<ARealTimeDroneReceiver> Loaded = SoftClass.LoadSynchronous();
		if (Loaded)
		{
			ReceiverDroneClass = Loaded;
			UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Loaded BP_RealTimeDrone successfully"));
		}
		else
		{
			ReceiverDroneClass = ARealTimeDroneReceiver::StaticClass();
			UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: BP_RealTimeDrone not found, falling back to C++ base class"));
		}
	}

	{
		TSoftClassPtr<AMultiDroneCharacter> SoftClass(FSoftObjectPath(TEXT("/Game/DroneOps/Blueprints/BP_MultiDroneCharacter.BP_MultiDroneCharacter_C")));
		TSubclassOf<AMultiDroneCharacter> Loaded = SoftClass.LoadSynchronous();
		if (Loaded)
		{
			ShadowDroneClass = Loaded;
			UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Loaded BP_MultiDroneCharacter successfully"));
		}
		else
		{
			ShadowDroneClass = AMultiDroneCharacter::StaticClass();
			UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: BP_MultiDroneCharacter not found, falling back to C++ base class"));
		}
	}

	// Initialize coordinate service
	InitializeCoordinateService();

	// Apply pending Georeference origin from main menu settings (must run after Cesium service init)
	ApplyPendingGeoreferenceOrigin();

	// Initialize drone registry
	InitializeDroneRegistry();

	// If a pending origin was applied, defer spawn until Georeference finishes updating.
	// Otherwise spawn immediately (direct level launch, no origin change).
	if (!bPendingSpawnAfterGeoreferenceUpdate)
	{
		SpawnReceiversFromRegistry();
	}

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

ARealTimeDroneReceiver* ADroneOpsGameMode::FindReceiverForDroneId(int32 DroneId) const
{
	UWorld* World = GetWorld();
	if (!World || DroneId <= 0)
	{
		return nullptr;
	}

	for (TActorIterator<ARealTimeDroneReceiver> It(World); It; ++It)
	{
		ARealTimeDroneReceiver* Receiver = *It;
		if (IsValid(Receiver) && Receiver->DroneId == DroneId)
		{
			return Receiver;
		}
	}

	return nullptr;
}

AMultiDroneCharacter* ADroneOpsGameMode::FindShadowForDroneId(int32 DroneId) const
{
	UWorld* World = GetWorld();
	if (!World || DroneId <= 0)
	{
		return nullptr;
	}

	for (TActorIterator<AMultiDroneCharacter> It(World); It; ++It)
	{
		AMultiDroneCharacter* Shadow = *It;
		if (IsValid(Shadow) && Shadow->DroneId == DroneId)
		{
			return Shadow;
		}
	}

	return nullptr;
}

void ADroneOpsGameMode::SpawnReceiversFromRegistry()
{
	if (!bSpawnReceiversFromRegistry)
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = GetGameInstance();
	if (!World || !GameInstance)
	{
		return;
	}

	UDroneRegistrySubsystem* Registry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry)
	{
		return;
	}

	const FVector SpawnBase = ReceiverSpawnOrigin;

	TArray<FDroneDescriptor> Descriptors = Registry->GetAllDroneDescriptors();
	Descriptors.Sort([](const FDroneDescriptor& A, const FDroneDescriptor& B)
	{
		const int32 AOrder = A.Slot > 0 ? A.Slot : A.DroneId;
		const int32 BOrder = B.Slot > 0 ? B.Slot : B.DroneId;
		return AOrder < BOrder;
	});

	TSubclassOf<ARealTimeDroneReceiver> ReceiverSpawnClass = ReceiverDroneClass
		? ReceiverDroneClass
		: TSubclassOf<ARealTimeDroneReceiver>(ARealTimeDroneReceiver::StaticClass());

	TSubclassOf<AMultiDroneCharacter> ShadowSpawnClass = ShadowDroneClass
		? ShadowDroneClass
		: TSubclassOf<AMultiDroneCharacter>(AMultiDroneCharacter::StaticClass());

	int32 SpawnIndex = 0;
	for (const FDroneDescriptor& Desc : Descriptors)
	{
		if (Desc.DroneId <= 0)
		{
			continue;
		}

		FDroneTelemetrySnapshot Snapshot;
		EDroneAvailability InitialAvailability = EDroneAvailability::Lost;
		if (Registry->GetTelemetry(Desc.DroneId, Snapshot))
		{
			InitialAvailability = Snapshot.Availability;
		}

		const FVector SpawnLocation = SpawnBase + FVector(SpawnIndex * ReceiverSpawnSpacingCm, 0.0f, 0.0f);
		const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);

		// ---- 镜像机 ----
		ARealTimeDroneReceiver* Receiver = FindReceiverForDroneId(Desc.DroneId);
		if (!Receiver)
		{
			Receiver = World->SpawnActorDeferred<ARealTimeDroneReceiver>(
				ReceiverSpawnClass, SpawnTransform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Receiver)
			{
				Receiver->ApplyDescriptor(Desc, InitialAvailability);
				Receiver->FinishSpawning(SpawnTransform);
			}
		}
		else
		{
			Receiver->SetActorLocation(SpawnLocation);
			Receiver->ApplyDescriptor(Desc, InitialAvailability);
		}

		// ---- 影子机（与镜像机初始位置相同）----
		if (bSpawnShadowDrones)
		{
			AMultiDroneCharacter* Shadow = FindShadowForDroneId(Desc.DroneId);
			if (!Shadow)
			{
				Shadow = World->SpawnActorDeferred<AMultiDroneCharacter>(
					ShadowSpawnClass, SpawnTransform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Shadow)
				{
					Shadow->DroneId         = Desc.DroneId;
					Shadow->DroneName       = Desc.Name.IsEmpty()
						? FString::Printf(TEXT("UAV-%d"), Desc.DroneId)
						: Desc.Name;
					Shadow->MavlinkSystemId = Desc.MavlinkSystemId;
					Shadow->BitIndex        = Desc.BitIndex;
					Shadow->ThemeColor      = Desc.ThemeColor;
					Shadow->UEReceivePort   = Desc.UEReceivePort;
					Shadow->TopicPrefix     = Desc.TopicPrefix;
					Shadow->FinishSpawning(SpawnTransform);
					UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Spawned shadow drone %s (ID=%d)"), *Shadow->DroneName, Desc.DroneId);
				}
			}
			else
			{
				Shadow->SetActorLocation(SpawnLocation);
				UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Shadow drone for ID=%d already exists, moved to spawn location"), Desc.DroneId);
			}
		}

		if (Receiver)
		{
			++SpawnIndex;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: ensured %d receiver drones from registry"), SpawnIndex);
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
{	UWorld* World = GetWorld();
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

void ADroneOpsGameMode::ApplyPendingGeoreferenceOrigin()
{
	if (!bUseCesiumCoordinates)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr || !NetMgr->HasPendingGeoreferenceOrigin())
	{
		return;
	}

	const double Lat = NetMgr->PendingOriginLatitude;
	const double Lon = NetMgr->PendingOriginLongitude;
	const double Alt = NetMgr->PendingOriginAltitude;
	NetMgr->ClearPendingGeoreferenceOrigin();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACesiumGeoreference> It(World); It; ++It)
	{
		ACesiumGeoreference* Georeference = *It;
		if (!IsValid(Georeference))
		{
			continue;
		}

		// Bind update event next tick to skip Cesium's own initialization broadcast.
		bPendingSpawnAfterGeoreferenceUpdate = true;
		GetWorld()->GetTimerManager().SetTimerForNextTick([this, Georeference, Lat, Lon, Alt]()
		{
			Georeference->OnGeoreferenceUpdated.AddDynamic(this, &ADroneOpsGameMode::OnGeoreferenceUpdated);
			Georeference->SetOriginLatitude(Lat);
			Georeference->SetOriginLongitude(Lon);
			Georeference->SetOriginHeight(Alt);
			UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: CesiumGeoreference origin set to (%.6f, %.6f, %.1fm), waiting for update..."),
				Lat, Lon, Alt);
		});
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: ApplyPendingGeoreferenceOrigin — no ACesiumGeoreference found in level"));
}

void ADroneOpsGameMode::OnGeoreferenceUpdated()
{
	if (!bPendingSpawnAfterGeoreferenceUpdate)
	{
		return;
	}
	bPendingSpawnAfterGeoreferenceUpdate = false;

	// Unbind so this only fires once.
	UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<ACesiumGeoreference> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				(*It)->OnGeoreferenceUpdated.RemoveDynamic(this, &ADroneOpsGameMode::OnGeoreferenceUpdated);
				break;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Georeference updated, spawning receivers now"));
	SpawnReceiversFromRegistry();
}
