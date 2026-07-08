// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsGameMode.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/SimpleCoordinateService.h"
#include "DroneOps/Core/CesiumCoordinateService.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOpsPlayerController.h"
#include "MultiDroneCharacter.h"
#include "RealTimeDroneReceiver.h"
#include "Cesium3DTileset.h"
#include "CesiumGeoreference.h"
#include "CesiumRasterOverlay.h"
#include "CesiumSunSky.h"
#include "CesiumTileMapServiceRasterOverlay.h"
#include "CesiumUrlTemplateRasterOverlay.h"
#include "CesiumWebMapTileServiceRasterOverlay.h"
#include "EngineUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"

namespace
{
FString NormalizeCesiumConfiguredHttpUrl(const FString& ConfiguredUrl)
{
	FString Url = ConfiguredUrl.TrimStartAndEnd();
	if (Url.IsEmpty() ||
		Url.Contains(TEXT("://")) ||
		Url.StartsWith(TEXT("{")) ||
		Url.StartsWith(TEXT("/")))
	{
		return Url;
	}

	return TEXT("http://") + Url;
}

FString ExpandCesiumUrlTemplateTokens(const FString& ConfiguredUrl)
{
	FString Url = ConfiguredUrl;

	// Unreal ini parsing can strip raw "{z}"-style tokens. Allow a config-safe
	// "$z" form and convert it back to Cesium's required URL template syntax.
	Url.ReplaceInline(TEXT("$reverseX"), TEXT("{reverseX}"), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("$reverseY"), TEXT("{reverseY}"), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("$reverseZ"), TEXT("{reverseZ}"), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("$x"), TEXT("{x}"), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("$y"), TEXT("{y}"), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("$z"), TEXT("{z}"), ESearchCase::CaseSensitive);

	return Url;
}

bool HasCesiumUrlTemplateTokens(const FString& Url)
{
	return Url.Contains(TEXT("{x}")) || Url.Contains(TEXT("{y}")) || Url.Contains(TEXT("{z}")) ||
		Url.Contains(TEXT("{reverseX}")) || Url.Contains(TEXT("{reverseY}")) || Url.Contains(TEXT("{reverseZ}")) ||
		Url.Contains(TEXT("{TileCol}")) || Url.Contains(TEXT("{TileRow}")) || Url.Contains(TEXT("{TileMatrix}"));
}

FString BuildCesiumLocalRasterTemplateUrl(const FString& LocalTileServerUrl)
{
	FString Url = ExpandCesiumUrlTemplateTokens(NormalizeCesiumConfiguredHttpUrl(LocalTileServerUrl));
	if (HasCesiumUrlTemplateTokens(Url))
	{
		return Url;
	}

	Url.RemoveFromEnd(TEXT("/"));
	return Url + TEXT("/{z}/{x}/{y}.png");
}

// Cesium FromUrl tileset 源需要指向 tileset.json；配置裸服务器地址时自动补全
FString BuildCesiumLocalTilesetUrl(const FString& LocalTileServerUrl)
{
	FString Url = NormalizeCesiumConfiguredHttpUrl(LocalTileServerUrl);
	if (Url.EndsWith(TEXT(".json")))
	{
		return Url;
	}

	Url.RemoveFromEnd(TEXT("/"));
	return Url + TEXT("/tileset.json");
}

bool LonLatToWebMercatorTile(double Longitude, double Latitude, int32 Zoom, int64& OutX, int64& OutY)
{
	if (Zoom < 0)
	{
		return false;
	}

	const double ClampedLatitude = FMath::Clamp(Latitude, -85.05112878, 85.05112878);
	const double N = FMath::Pow(2.0, static_cast<double>(Zoom));
	const double LatRad = FMath::DegreesToRadians(ClampedLatitude);
	const double X = (Longitude + 180.0) / 360.0 * N;
	const double Y = (1.0 - FMath::Loge(FMath::Tan(LatRad) + (1.0 / FMath::Cos(LatRad))) / UE_DOUBLE_PI) * 0.5 * N;

	OutX = static_cast<int64>(FMath::FloorToDouble(X));
	OutY = static_cast<int64>(FMath::FloorToDouble(Y));

	const int64 MaxTile = (static_cast<int64>(1) << Zoom) - 1;
	OutX = FMath::Clamp<int64>(OutX, 0, MaxTile);
	OutY = FMath::Clamp<int64>(OutY, 0, MaxTile);
	return true;
}

FString BuildRasterTileUrlFromTemplate(const FString& TemplateUrl, int32 Zoom, int64 X, int64 Y)
{
	const int64 MaxTile = (static_cast<int64>(1) << Zoom) - 1;
	FString Url = TemplateUrl;
	Url.ReplaceInline(TEXT("{z}"), *FString::FromInt(Zoom), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("{x}"), *FString::Printf(TEXT("%lld"), X), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("{y}"), *FString::Printf(TEXT("%lld"), Y), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("{reverseX}"), *FString::Printf(TEXT("%lld"), MaxTile - X), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("{reverseY}"), *FString::Printf(TEXT("%lld"), MaxTile - Y), ESearchCase::CaseSensitive);
	Url.ReplaceInline(TEXT("{reverseZ}"), TEXT("0"), ESearchCase::CaseSensitive);
	return Url;
}

double WebMercatorTileXToLongitude(int64 X, int32 Zoom)
{
	const double N = FMath::Pow(2.0, static_cast<double>(Zoom));
	return (static_cast<double>(X) / N * 360.0) - 180.0;
}

double WebMercatorTileYToLatitude(int64 Y, int32 Zoom)
{
	const double N = FMath::Pow(2.0, static_cast<double>(Zoom));
	const double MercatorN = UE_DOUBLE_PI - (2.0 * UE_DOUBLE_PI * static_cast<double>(Y) / N);
	return FMath::RadiansToDegrees(FMath::Atan(FMath::Sinh(MercatorN)));
}

UTexture2D* CreateTextureFromCompressedImage(const TArray<uint8>& CompressedImage, UObject* Outer)
{
	if (CompressedImage.IsEmpty() || !Outer)
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedImage.GetData(), CompressedImage.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedImage.GetData(), CompressedImage.Num()))
	{
		return nullptr;
	}

	TArray64<uint8> RawPixels;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawPixels))
	{
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	if (Width <= 0 || Height <= 0 || RawPixels.Num() != static_cast<int64>(Width) * Height * 4)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	Texture->Rename(nullptr, Outer);
	Texture->SRGB = true;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_NoMipmaps;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, RawPixels.GetData(), RawPixels.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}
}

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

	ApplyCesiumTileServerConfig();

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
			PrefetchLocalRasterTilesAround(Lat, Lon);
			EnsureOfflineCesiumSunSky(Lat, Lon);
			CreateOfflineRasterPlaneAround(Lat, Lon, Alt);
		});
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: ApplyPendingGeoreferenceOrigin — no ACesiumGeoreference found in level"));
}

void ADroneOpsGameMode::PrefetchLocalRasterTilesAround(double Latitude, double Longitude)
{
	bool bUseLocalTileServer = false;
	bool bPrefetchLocalRasterTiles = true;
	FString LocalTileServerUrl = TEXT("http://localhost:8070");
	FString RasterTemplateUrlConfig;
	int32 PrefetchMinimumLevel = 14;
	int32 PrefetchMaximumLevel = 16;
	int32 PrefetchRadius = 2;
	int32 PrefetchMaxRequests = 96;

	if (GConfig)
	{
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseLocalTileServer"), bUseLocalTileServer, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("PrefetchLocalRasterTiles"), bPrefetchLocalRasterTiles, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("LocalTileServerUrl"), LocalTileServerUrl, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("RasterTemplateUrl"), RasterTemplateUrlConfig, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("PrefetchMinimumLevel"), PrefetchMinimumLevel, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("PrefetchMaximumLevel"), PrefetchMaximumLevel, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("PrefetchRadius"), PrefetchRadius, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("PrefetchMaxRequests"), PrefetchMaxRequests, GEngineIni);
	}

	if (!bUseLocalTileServer || !bPrefetchLocalRasterTiles)
	{
		return;
	}

	const FString RasterTemplateUrl = RasterTemplateUrlConfig.TrimStartAndEnd().IsEmpty()
		? BuildCesiumLocalRasterTemplateUrl(LocalTileServerUrl)
		: ExpandCesiumUrlTemplateTokens(NormalizeCesiumConfiguredHttpUrl(RasterTemplateUrlConfig));

	if (!HasCesiumUrlTemplateTokens(RasterTemplateUrl))
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Skipping raster prefetch because RasterTemplateUrl '%s' has no usable Cesium URL template tokens."),
			*RasterTemplateUrl);
		return;
	}

	PrefetchMinimumLevel = FMath::Max(0, PrefetchMinimumLevel);
	PrefetchMaximumLevel = FMath::Max(PrefetchMinimumLevel, PrefetchMaximumLevel);
	PrefetchRadius = FMath::Clamp(PrefetchRadius, 0, 16);
	PrefetchMaxRequests = FMath::Max(0, PrefetchMaxRequests);
	if (PrefetchMaxRequests == 0)
	{
		return;
	}

	int32 RequestCount = 0;
	for (int32 Zoom = PrefetchMinimumLevel; Zoom <= PrefetchMaximumLevel && RequestCount < PrefetchMaxRequests; ++Zoom)
	{
		int64 CenterX = 0;
		int64 CenterY = 0;
		if (!LonLatToWebMercatorTile(Longitude, Latitude, Zoom, CenterX, CenterY))
		{
			continue;
		}

		const int64 MaxTile = (static_cast<int64>(1) << Zoom) - 1;
		for (int64 DY = -PrefetchRadius; DY <= PrefetchRadius && RequestCount < PrefetchMaxRequests; ++DY)
		{
			for (int64 DX = -PrefetchRadius; DX <= PrefetchRadius && RequestCount < PrefetchMaxRequests; ++DX)
			{
				const int64 X = FMath::Clamp<int64>(CenterX + DX, 0, MaxTile);
				const int64 Y = FMath::Clamp<int64>(CenterY + DY, 0, MaxTile);
				const FString TileUrl = BuildRasterTileUrlFromTemplate(RasterTemplateUrl, Zoom, X, Y);

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
				Request->SetURL(TileUrl);
				Request->SetVerb(TEXT("GET"));
				Request->SetTimeout(5.0f);
				Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bSucceeded)
				{
					if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() >= 400)
					{
						UE_LOG(LogTemp, Verbose, TEXT("DroneOpsGameMode: Local raster prefetch missed '%s' status=%d"),
							CompletedRequest.IsValid() ? *CompletedRequest->GetURL() : TEXT("<invalid>"),
							Response.IsValid() ? Response->GetResponseCode() : -1);
					}
				});
				Request->ProcessRequest();
				++RequestCount;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Prefetching %d local raster tiles around lat=%.6f lon=%.6f, zoom=%d-%d, radius=%d."),
		RequestCount,
		Latitude,
		Longitude,
		PrefetchMinimumLevel,
		PrefetchMaximumLevel,
		PrefetchRadius);
}

void ADroneOpsGameMode::EnsureOfflineCesiumSunSky(double Latitude, double Longitude)
{
	bool bUseLocalTileServer = false;
	bool bUseOfflineCesiumSunSky = true;
	double SolarTime = 13.0;
	float SkyLightIntensity = 2.0f;

	if (GConfig)
	{
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseLocalTileServer"), bUseLocalTileServer, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseOfflineCesiumSunSky"), bUseOfflineCesiumSunSky, GEngineIni);
		GConfig->GetDouble(TEXT("CesiumTileServer"), TEXT("OfflineSunSkySolarTime"), SolarTime, GEngineIni);
		GConfig->GetFloat(TEXT("CesiumTileServer"), TEXT("OfflineSunSkySkyLightIntensity"), SkyLightIntensity, GEngineIni);
	}

	if (!bUseLocalTileServer || !bUseOfflineCesiumSunSky)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ACesiumSunSky* SunSky = nullptr;
	for (TActorIterator<ACesiumSunSky> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			SunSky = *It;
			break;
		}
	}

	if (!SunSky)
	{
		SunSky = World->SpawnActor<ACesiumSunSky>(ACesiumSunSky::StaticClass(), FTransform::Identity);
		if (!SunSky)
		{
			UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Failed to spawn CesiumSunSky for offline raster mode."));
			return;
		}
#if WITH_EDITOR
		SunSky->SetActorLabel(TEXT("DroneOpsOfflineCesiumSunSky"));
#endif
	}

	SunSky->SetActorHiddenInGame(false);
	SunSky->SetActorEnableCollision(false);
	SunSky->SolarTime = FMath::Clamp(SolarTime, 0.0, 23.9999);
	SunSky->EstimateTimeZoneForLongitude(Longitude);
	if (SunSky->SkyLight)
	{
		SunSky->SkyLight->SetIntensity(SkyLightIntensity);
		SunSky->SkyLight->SetVisibility(true);
	}
	SunSky->UpdateSun();

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Ensured CesiumSunSky for offline raster mode at lat=%.6f lon=%.6f solarTime=%.2f skylight=%.2f."),
		Latitude,
		Longitude,
		SunSky->SolarTime,
		SkyLightIntensity);
}

void ADroneOpsGameMode::CreateOfflineRasterPlaneAround(double Latitude, double Longitude, double HeightMeters)
{
	bool bUseLocalTileServer = false;
	bool bUseOfflineRasterPlane = true;
	FString LocalTileServerUrl = TEXT("http://localhost:8070");
	FString RasterTemplateUrlConfig;
	FString PlaneMaterialPath = TEXT("/Game/DroneOps/Materials/M_OfflineRasterTile.M_OfflineRasterTile");
	bool bUsePlaneLod = true;
	int32 PlaneZoom = 18;
	int32 PlaneRadius = 4;
	int32 PlaneFarZoom = 16;
	int32 PlaneFarRadius = 8;
	int32 PlaneMidZoom = 17;
	int32 PlaneMidRadius = 6;
	int32 PlaneNearZoom = 18;
	int32 PlaneNearRadius = 4;
	int32 PlaneMaxTiles = 600;
	float PlaneVerticalOffsetCm = 0.0f;
	float PlaneLodVerticalStepCm = 2.0f;
	bool bPlaneCollision = true;

	if (GConfig)
	{
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseLocalTileServer"), bUseLocalTileServer, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseOfflineRasterPlane"), bUseOfflineRasterPlane, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("LocalTileServerUrl"), LocalTileServerUrl, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("RasterTemplateUrl"), RasterTemplateUrlConfig, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneMaterial"), PlaneMaterialPath, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseOfflineRasterPlaneLod"), bUsePlaneLod, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneZoom"), PlaneZoom, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneRadius"), PlaneRadius, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneFarZoom"), PlaneFarZoom, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneFarRadius"), PlaneFarRadius, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneMidZoom"), PlaneMidZoom, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneMidRadius"), PlaneMidRadius, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneNearZoom"), PlaneNearZoom, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneNearRadius"), PlaneNearRadius, GEngineIni);
		GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneMaxTiles"), PlaneMaxTiles, GEngineIni);
		GConfig->GetFloat(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneVerticalOffsetCm"), PlaneVerticalOffsetCm, GEngineIni);
		GConfig->GetFloat(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneLodVerticalStepCm"), PlaneLodVerticalStepCm, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("OfflineRasterPlaneCollision"), bPlaneCollision, GEngineIni);
	}

	if (!bUseLocalTileServer || !bUseOfflineRasterPlane)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ACesiumGeoreference* Georeference = nullptr;
	for (TActorIterator<ACesiumGeoreference> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			Georeference = *It;
			break;
		}
	}

	if (!Georeference)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Cannot create offline raster plane because no ACesiumGeoreference was found."));
		return;
	}

	const FString RasterTemplateUrl = RasterTemplateUrlConfig.TrimStartAndEnd().IsEmpty()
		? BuildCesiumLocalRasterTemplateUrl(LocalTileServerUrl)
		: ExpandCesiumUrlTemplateTokens(NormalizeCesiumConfiguredHttpUrl(RasterTemplateUrlConfig));

	if (!HasCesiumUrlTemplateTokens(RasterTemplateUrl))
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Cannot create offline raster plane because RasterTemplateUrl '%s' has no tile tokens."),
			*RasterTemplateUrl);
		return;
	}

	struct FOfflineRasterPlaneLod
	{
		int32 Zoom = 0;
		int32 Radius = 0;
		int32 LayerIndex = 0;
	};

	TArray<FOfflineRasterPlaneLod> LodLevels;
	if (bUsePlaneLod)
	{
		LodLevels.Add({ FMath::Clamp(PlaneFarZoom, 0, 30), FMath::Clamp(PlaneFarRadius, 0, 32), 0 });
		LodLevels.Add({ FMath::Clamp(PlaneMidZoom, 0, 30), FMath::Clamp(PlaneMidRadius, 0, 32), 1 });
		LodLevels.Add({ FMath::Clamp(PlaneNearZoom, 0, 30), FMath::Clamp(PlaneNearRadius, 0, 32), 2 });
	}
	else
	{
		LodLevels.Add({ FMath::Clamp(PlaneZoom, 0, 30), FMath::Clamp(PlaneRadius, 0, 32), 0 });
	}

	LodLevels.Sort([](const FOfflineRasterPlaneLod& A, const FOfflineRasterPlaneLod& B)
	{
		return A.Zoom < B.Zoom;
	});

	PlaneMaxTiles = FMath::Max(0, PlaneMaxTiles);
	if (PlaneMaxTiles == 0)
	{
		return;
	}
	PlaneLodVerticalStepCm = FMath::Clamp(PlaneLodVerticalStepCm, 0.0f, 50.0f);

	const FName PlaneTag(TEXT("DroneOpsOfflineRasterPlane"));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* ExistingActor = *It;
		if (IsValid(ExistingActor) && ExistingActor->Tags.Contains(PlaneTag))
		{
			ExistingActor->Destroy();
		}
	}

	AActor* PlaneRoot = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	if (!PlaneRoot)
	{
		return;
	}
	PlaneRoot->Tags.AddUnique(PlaneTag);
	PlaneRoot->SetActorHiddenInGame(false);

	USceneComponent* PlaneSceneRoot = NewObject<USceneComponent>(PlaneRoot, TEXT("OfflineRasterPlaneRoot"));
	PlaneRoot->SetRootComponent(PlaneSceneRoot);
	PlaneSceneRoot->RegisterComponent();

	UMaterialInterface* PlaneMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *PlaneMaterialPath));
	if (!PlaneMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Offline raster plane material '%s' was not found. Create it with a Texture2D parameter named TileTexture."),
			*PlaneMaterialPath);
	}

	const FVector CenterWorld = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Longitude, Latitude, HeightMeters));
	int32 CreatedTiles = 0;

	for (const FOfflineRasterPlaneLod& Lod : LodLevels)
	{
		int64 CenterX = 0;
		int64 CenterY = 0;
		if (!LonLatToWebMercatorTile(Longitude, Latitude, Lod.Zoom, CenterX, CenterY))
		{
			continue;
		}

		const int64 MaxTile = (static_cast<int64>(1) << Lod.Zoom) - 1;
		const float PlaneZ = static_cast<float>(CenterWorld.Z + PlaneVerticalOffsetCm + (PlaneLodVerticalStepCm * static_cast<float>(Lod.LayerIndex)));

		for (int64 DY = -Lod.Radius; DY <= Lod.Radius && CreatedTiles < PlaneMaxTiles; ++DY)
		{
			for (int64 DX = -Lod.Radius; DX <= Lod.Radius && CreatedTiles < PlaneMaxTiles; ++DX)
			{
				const int64 TileX = FMath::Clamp<int64>(CenterX + DX, 0, MaxTile);
				const int64 TileY = FMath::Clamp<int64>(CenterY + DY, 0, MaxTile);

				const double West = WebMercatorTileXToLongitude(TileX, Lod.Zoom);
				const double East = WebMercatorTileXToLongitude(TileX + 1, Lod.Zoom);
				const double North = WebMercatorTileYToLatitude(TileY, Lod.Zoom);
				const double South = WebMercatorTileYToLatitude(TileY + 1, Lod.Zoom);

				FVector Northwest = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(West, North, HeightMeters));
				FVector Northeast = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(East, North, HeightMeters));
				FVector Southeast = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(East, South, HeightMeters));
				FVector Southwest = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(West, South, HeightMeters));
				Northwest.Z = PlaneZ;
				Northeast.Z = PlaneZ;
				Southeast.Z = PlaneZ;
				Southwest.Z = PlaneZ;

				TArray<FVector> Vertices;
				Vertices.Add(Northwest);
				Vertices.Add(Northeast);
				Vertices.Add(Southeast);
				Vertices.Add(Southwest);

				TArray<int32> Triangles;
				Triangles.Add(0);
				Triangles.Add(1);
				Triangles.Add(2);
				Triangles.Add(0);
				Triangles.Add(2);
				Triangles.Add(3);

				TArray<FVector> Normals;
				Normals.Init(FVector::UpVector, 4);

				TArray<FVector2D> UVs;
				UVs.Add(FVector2D(0.0f, 0.0f));
				UVs.Add(FVector2D(1.0f, 0.0f));
				UVs.Add(FVector2D(1.0f, 1.0f));
				UVs.Add(FVector2D(0.0f, 1.0f));

				TArray<FColor> VertexColors;
				VertexColors.Init(FColor::White, 4);

				TArray<FProcMeshTangent> Tangents;
				Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), 4);

				const FName ComponentName(*FString::Printf(TEXT("OfflineRasterTile_LOD%d_%d_%lld_%lld"), Lod.LayerIndex, Lod.Zoom, TileX, TileY));
				UProceduralMeshComponent* TileMesh = NewObject<UProceduralMeshComponent>(PlaneRoot, ComponentName);
				if (!TileMesh)
				{
					continue;
				}

				TileMesh->SetupAttachment(PlaneSceneRoot);
				TileMesh->RegisterComponent();
				PlaneRoot->AddInstanceComponent(TileMesh);
				TileMesh->SetCollisionEnabled(bPlaneCollision ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
				TileMesh->SetCollisionProfileName(bPlaneCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
				TileMesh->SetGenerateOverlapEvents(false);
				TileMesh->bUseComplexAsSimpleCollision = true;
				TileMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bPlaneCollision);

				if (PlaneMaterial)
				{
					UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(PlaneMaterial, TileMesh);
					TileMesh->SetMaterial(0, DynamicMaterial);

					const FString TileUrl = BuildRasterTileUrlFromTemplate(RasterTemplateUrl, Lod.Zoom, TileX, TileY);
					TWeakObjectPtr<UProceduralMeshComponent> WeakTileMesh(TileMesh);
					TWeakObjectPtr<UMaterialInstanceDynamic> WeakMaterial(DynamicMaterial);
					TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
					Request->SetURL(TileUrl);
					Request->SetVerb(TEXT("GET"));
					Request->SetTimeout(8.0f);
					Request->OnProcessRequestComplete().BindLambda([WeakTileMesh, WeakMaterial](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bSucceeded)
					{
						if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() >= 400)
						{
							UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Offline raster plane tile request failed '%s' status=%d"),
								CompletedRequest.IsValid() ? *CompletedRequest->GetURL() : TEXT("<invalid>"),
								Response.IsValid() ? Response->GetResponseCode() : -1);
							return;
						}

						if (!WeakTileMesh.IsValid() || !WeakMaterial.IsValid())
						{
							return;
						}

						UTexture2D* TileTexture = CreateTextureFromCompressedImage(Response->GetContent(), WeakTileMesh.Get());
						if (!TileTexture)
						{
							UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: Failed to decode offline raster tile '%s'."),
								CompletedRequest.IsValid() ? *CompletedRequest->GetURL() : TEXT("<invalid>"));
							return;
						}

						WeakMaterial->SetTextureParameterValue(TEXT("TileTexture"), TileTexture);
					});
					Request->ProcessRequest();
				}

				++CreatedTiles;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Created offline raster plane tiles=%d lod=%s collision=%s center=(lat=%.6f lon=%.6f height=%.1fm) material='%s'."),
		CreatedTiles,
		bUsePlaneLod ? TEXT("true") : TEXT("false"),
		bPlaneCollision ? TEXT("true") : TEXT("false"),
		Latitude,
		Longitude,
		HeightMeters,
		*PlaneMaterialPath);
}

void ADroneOpsGameMode::ApplyCesiumTileServerConfig()
{
	bool bUseLocalTileServer = false;
	bool bSwitchTilesetsToLocal = true;
	bool bCreateUrlTemplateRasterOverlay = true;
	bool bDisableNonUrlRasterOverlays = true;
	FString LocalTileServerUrl = TEXT("http://localhost:8070");
	FString RasterTemplateUrlConfig;
	FString TilesetUrlConfig;
	FString RasterProjection = TEXT("WebMercator");
	int32 RasterTileWidth = 256;
	int32 RasterTileHeight = 256;
	int32 RasterMinimumLevel = 0;
	int32 RasterMaximumLevel = 25;
	bool bHasRasterTileWidth = false;
	bool bHasRasterTileHeight = false;
	bool bHasRasterMinimumLevel = false;
	bool bHasRasterMaximumLevel = false;

	if (GConfig)
	{
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("UseLocalTileServer"), bUseLocalTileServer, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("SwitchTilesetsToLocal"), bSwitchTilesetsToLocal, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("CreateUrlTemplateRasterOverlay"), bCreateUrlTemplateRasterOverlay, GEngineIni);
		GConfig->GetBool(TEXT("CesiumTileServer"), TEXT("DisableNonUrlRasterOverlays"), bDisableNonUrlRasterOverlays, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("LocalTileServerUrl"), LocalTileServerUrl, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("RasterTemplateUrl"), RasterTemplateUrlConfig, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("TilesetUrl"), TilesetUrlConfig, GEngineIni);
		GConfig->GetString(TEXT("CesiumTileServer"), TEXT("RasterProjection"), RasterProjection, GEngineIni);
		bHasRasterTileWidth = GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("RasterTileWidth"), RasterTileWidth, GEngineIni);
		bHasRasterTileHeight = GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("RasterTileHeight"), RasterTileHeight, GEngineIni);
		bHasRasterMinimumLevel = GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("RasterMinimumLevel"), RasterMinimumLevel, GEngineIni);
		bHasRasterMaximumLevel = GConfig->GetInt(TEXT("CesiumTileServer"), TEXT("RasterMaximumLevel"), RasterMaximumLevel, GEngineIni);
	}

	if (!bUseLocalTileServer)
	{
		UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Cesium local tile server disabled; keeping online Cesium/Google sources."));
		return;
	}

	LocalTileServerUrl = NormalizeCesiumConfiguredHttpUrl(LocalTileServerUrl);
	if (LocalTileServerUrl.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: UseLocalTileServer=true but LocalTileServerUrl is empty; keeping existing Cesium sources."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString RasterTemplateUrl = RasterTemplateUrlConfig.TrimStartAndEnd().IsEmpty()
		? BuildCesiumLocalRasterTemplateUrl(LocalTileServerUrl)
		: ExpandCesiumUrlTemplateTokens(NormalizeCesiumConfiguredHttpUrl(RasterTemplateUrlConfig));
	const FString TilesetUrl = TilesetUrlConfig.TrimStartAndEnd().IsEmpty()
		? BuildCesiumLocalTilesetUrl(LocalTileServerUrl)
		: NormalizeCesiumConfiguredHttpUrl(TilesetUrlConfig);

	if (!HasCesiumUrlTemplateTokens(RasterTemplateUrl))
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsGameMode: RasterTemplateUrl '%s' has no Cesium URL template tokens. Use config-safe tokens like $z, $x, $y in DefaultEngine.ini."),
			*RasterTemplateUrl);
	}
	const int32 ClampedRasterTileWidth = FMath::Clamp(RasterTileWidth, 64, 2048);
	const int32 ClampedRasterTileHeight = FMath::Clamp(RasterTileHeight, 64, 2048);
	const int32 ClampedRasterMinimumLevel = FMath::Max(0, RasterMinimumLevel);
	const int32 ClampedRasterMaximumLevel = FMath::Max(ClampedRasterMinimumLevel, RasterMaximumLevel);
	const ECesiumUrlTemplateRasterOverlayProjection UrlTemplateProjection =
		RasterProjection.Equals(TEXT("Geographic"), ESearchCase::IgnoreCase)
		? ECesiumUrlTemplateRasterOverlayProjection::Geographic
		: ECesiumUrlTemplateRasterOverlayProjection::WebMercator;
	int32 TilesetCount = 0;
	int32 RasterOverlayCount = 0;
	int32 CreatedRasterOverlayCount = 0;
	int32 DisabledRasterOverlayCount = 0;
	int32 UnsupportedOverlayCount = 0;

	for (TActorIterator<ACesium3DTileset> It(World); It; ++It)
	{
		ACesium3DTileset* Tileset = *It;
		if (!IsValid(Tileset))
		{
			continue;
		}

		if (bSwitchTilesetsToLocal)
		{
			Tileset->SetTilesetSource(ETilesetSource::FromUrl);
			Tileset->SetUrl(TilesetUrl);
			Tileset->RefreshTileset();
			++TilesetCount;
		}

		TArray<UCesiumRasterOverlay*> RasterOverlays;
		Tileset->GetComponents<UCesiumRasterOverlay>(RasterOverlays);
		bool bHasUrlTemplateOverlay = false;

		for (UCesiumRasterOverlay* Overlay : RasterOverlays)
		{
			if (IsValid(Overlay) && Overlay->IsA<UCesiumUrlTemplateRasterOverlay>())
			{
				bHasUrlTemplateOverlay = true;
				break;
			}
		}

		if (!bHasUrlTemplateOverlay && bCreateUrlTemplateRasterOverlay)
		{
			UCesiumUrlTemplateRasterOverlay* CreatedOverlay = NewObject<UCesiumUrlTemplateRasterOverlay>(
				Tileset,
				UCesiumUrlTemplateRasterOverlay::StaticClass(),
				TEXT("OvitLocalRasterOverlay"));
			if (IsValid(CreatedOverlay))
			{
				CreatedOverlay->MaterialLayerKey = TEXT("Overlay0");
				CreatedOverlay->RegisterComponent();
				Tileset->AddInstanceComponent(CreatedOverlay);
				RasterOverlays.Add(CreatedOverlay);
				++CreatedRasterOverlayCount;
				UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Created URL template raster overlay on '%s' for local tile service."),
					*Tileset->GetName());
			}
		}

		for (UCesiumRasterOverlay* Overlay : RasterOverlays)
		{
			if (!IsValid(Overlay))
			{
				continue;
			}

			if (UCesiumUrlTemplateRasterOverlay* UrlTemplateOverlay = Cast<UCesiumUrlTemplateRasterOverlay>(Overlay))
			{
				UrlTemplateOverlay->TemplateUrl = RasterTemplateUrl;
				UrlTemplateOverlay->Projection = UrlTemplateProjection;
				if (bHasRasterTileWidth)
				{
					UrlTemplateOverlay->TileWidth = ClampedRasterTileWidth;
				}
				if (bHasRasterTileHeight)
				{
					UrlTemplateOverlay->TileHeight = ClampedRasterTileHeight;
				}
				if (bHasRasterMinimumLevel)
				{
					UrlTemplateOverlay->MinimumLevel = ClampedRasterMinimumLevel;
				}
				if (bHasRasterMaximumLevel)
				{
					UrlTemplateOverlay->MaximumLevel = ClampedRasterMaximumLevel;
				}
				UrlTemplateOverlay->Refresh();
				++RasterOverlayCount;
			}
			else if (UCesiumTileMapServiceRasterOverlay* TmsOverlay = Cast<UCesiumTileMapServiceRasterOverlay>(Overlay))
			{
				TmsOverlay->Url = LocalTileServerUrl;
				TmsOverlay->Refresh();
				++RasterOverlayCount;
			}
			else if (UCesiumWebMapTileServiceRasterOverlay* WmtsOverlay = Cast<UCesiumWebMapTileServiceRasterOverlay>(Overlay))
			{
				WmtsOverlay->BaseUrl = RasterTemplateUrl;
				WmtsOverlay->Refresh();
				++RasterOverlayCount;
			}
			else
			{
				++UnsupportedOverlayCount;
				if (bDisableNonUrlRasterOverlays)
				{
					Overlay->Deactivate();
					++DisabledRasterOverlayCount;
				}
				UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Cesium overlay '%s' on '%s' is not URL-configurable at runtime; class=%s%s"),
					*Overlay->GetName(),
					*Tileset->GetName(),
					*Overlay->GetClass()->GetName(),
					bDisableNonUrlRasterOverlays ? TEXT("; deactivated for local raster mode") : TEXT(""));
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DroneOpsGameMode: Cesium local tile server enabled. SwitchTilesetsToLocal=%s Tilesets=%d Url='%s', URL raster overlays=%d created=%d disabled=%d TemplateUrl='%s', TileSize=%dx%d, Level=%d-%d, unsupported overlays=%d"),
		bSwitchTilesetsToLocal ? TEXT("true") : TEXT("false"),
		TilesetCount,
		*TilesetUrl,
		RasterOverlayCount,
		CreatedRasterOverlayCount,
		DisabledRasterOverlayCount,
		*RasterTemplateUrl,
		bHasRasterTileWidth ? ClampedRasterTileWidth : -1,
		bHasRasterTileHeight ? ClampedRasterTileHeight : -1,
		bHasRasterMinimumLevel ? ClampedRasterMinimumLevel : -1,
		bHasRasterMaximumLevel ? ClampedRasterMaximumLevel : -1,
		UnsupportedOverlayCount);
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
