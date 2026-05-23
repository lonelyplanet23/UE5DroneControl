// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

AMainMenuPlayerController::AMainMenuPlayerController()
{
	// 主菜单始终显示鼠标光标
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 设置输入模式为纯 UI（主菜单不需要游戏输入）
	FInputModeUIOnly InputMode;
	SetInputMode(InputMode);

	ShowMainMenuWidget();
}

void AMainMenuPlayerController::ShowMainMenuWidget()
{
	if (!MainMenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("AMainMenuPlayerController: MainMenuWidgetClass is not set. "
			"Please assign it in BP_MainMenuPlayerController."));
		return;
	}

	MainMenuWidgetInstance = CreateWidget<UUserWidget>(this, MainMenuWidgetClass);
	if (MainMenuWidgetInstance)
	{
		MainMenuWidgetInstance->AddToViewport(0);
	}
}

void AMainMenuPlayerController::GoToQueueEditor()
{
	UGameplayStatics::OpenLevel(this, QueueEditorLevelName);
}

void AMainMenuPlayerController::GoToPreview()
{
	UGameplayStatics::OpenLevel(this, PreviewLevelName);
}

void AMainMenuPlayerController::GoToPreviewWithOrigin(double Latitude, double Longitude, double Altitude)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
		{
			NetMgr->PendingOriginLatitude  = Latitude;
			NetMgr->PendingOriginLongitude = Longitude;
			NetMgr->PendingOriginAltitude  = Altitude;
		}
	}
	UGameplayStatics::OpenLevel(this, PreviewLevelName);
}

void AMainMenuPlayerController::GoToPreviewFromSavedSettings()
{
	const FString SavePath = FPaths::ProjectSavedDir() + TEXT("Settings.txt");
	FString LoadedString;

	if (FFileHelper::LoadFileToString(LoadedString, *SavePath))
	{
		TArray<FString> Parsed;
		LoadedString.ParseIntoArray(Parsed, TEXT("|"));

		// 格式：BackendAddress|Lat|Lon|Alt|bUseCesium  (5段)
		// 兼容旧格式：BackendAddress|Lat|Lon|bUseCesium (4段，无Alt)
		if (Parsed.Num() >= 5)
		{
			const double Lat = FCString::Atod(*Parsed[1]);
			const double Lon = FCString::Atod(*Parsed[2]);
			const double Alt = FCString::Atod(*Parsed[3]);
			GoToPreviewWithOrigin(Lat, Lon, Alt);
			return;
		}
		if (Parsed.Num() == 4)
		{
			const double Lat = FCString::Atod(*Parsed[1]);
			const double Lon = FCString::Atod(*Parsed[2]);
			GoToPreviewWithOrigin(Lat, Lon, 43.0); // 旧格式默认海拔43m
			return;
		}
	}

	// 文件不存在或格式无法解析，直接跳转不修改 Georeference
	UE_LOG(LogTemp, Warning, TEXT("GoToPreviewFromSavedSettings: no valid settings found, skipping Georeference update"));
	UGameplayStatics::OpenLevel(this, PreviewLevelName);
}
