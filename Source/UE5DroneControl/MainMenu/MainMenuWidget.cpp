// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"
#include "MainMenuPlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <fstream>
#include <string>
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"


void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 初始化时加载已保存设置（蓝图子类可在 Construct 事件中调用 LoadSettings）
}

// ---------------------------------------------------------------------------
// 无人机注册
// ---------------------------------------------------------------------------

void UMainMenuWidget::RegisterDrone(int32 DroneId, const FString& IpAddress)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry) return;

	if (UDroneNetworkManager* Network = GI->GetSubsystem<UDroneNetworkManager>())
	{
		FOnHttpResponse Callback;
		PendingBackendRegisterDroneId = DroneId;
		PendingBackendRegisterIpAddress = IpAddress;
		Callback.BindDynamic(this, &UMainMenuWidget::HandleBackendRegisterResponse);
		Network->RegisterDroneToBackend(DroneId, IpAddress, Callback);
		return;
	}

	FDroneDescriptor Desc;
	Desc.DroneId   = DroneId;
	Desc.Slot      = DroneId;
	Desc.Name      = FString::Printf(TEXT("UAV-%d"), DroneId);
	Desc.IpAddress = IpAddress;
	Desc.ControlPort = 8889 + (DroneId - 1) * 2;
	Desc.UEReceivePort = 8888 + (DroneId - 1) * 2;
	Desc.MavlinkSystemId = DroneId;
	Desc.BitIndex = DroneId > 0 ? DroneId - 1 : 0;
	Desc.TopicPrefix = FString::Printf(TEXT("/px4_%d"), DroneId);
	Registry->RegisterDrone(Desc);
	Registry->MarkDroneAvailability(DroneId, EDroneAvailability::Lost);
	OnDroneRegistered(DroneId);
}

void UMainMenuWidget::HandleBackendRegisterResponse(bool bSuccess, const FString& ResponseBody)
{
	const int32 DroneId = PendingBackendRegisterDroneId;
	const FString IpAddress = PendingBackendRegisterIpAddress;
	PendingBackendRegisterDroneId = 0;
	PendingBackendRegisterIpAddress.Reset();

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[MainMenu] Backend registered drone slot=%d: %s"), DroneId, *ResponseBody);
		OnDroneRegistered(DroneId);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Backend register failed for slot=%d: %s"), DroneId, *ResponseBody);

	UGameInstance* GI = GetGameInstance();
	UDroneRegistrySubsystem* Registry = GI ? GI->GetSubsystem<UDroneRegistrySubsystem>() : nullptr;
	if (Registry && DroneId > 0)
	{
		FDroneDescriptor Desc;
		Desc.DroneId = DroneId;
		Desc.Slot = DroneId;
		Desc.Name = FString::Printf(TEXT("UAV-%d"), DroneId);
		Desc.IpAddress = IpAddress;
		Desc.ControlPort = 8889 + (DroneId - 1) * 2;
		Desc.UEReceivePort = 8888 + (DroneId - 1) * 2;
		Desc.MavlinkSystemId = DroneId;
		Desc.BitIndex = DroneId > 0 ? DroneId - 1 : 0;
		Desc.TopicPrefix = FString::Printf(TEXT("/px4_%d"), DroneId);
		Registry->RegisterDrone(Desc);
		Registry->MarkDroneAvailability(DroneId, EDroneAvailability::Lost);
		OnDroneRegistered(DroneId);
	}
}

void UMainMenuWidget::UnregisterDrone(int32 DroneId)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry) return;

	Registry->UnregisterDrone(DroneId);
	OnDroneUnregistered(DroneId);
}

TArray<FString> UMainMenuWidget::GetRegisteredDronesSummary() const
{
	TArray<FString> Result;

	UGameInstance* GI = GetGameInstance();
	if (!GI) return Result;

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry) return Result;

	for (const FDroneDescriptor& Desc : Registry->GetAllDroneDescriptors())
	{
		Result.Add(FString::Printf(TEXT("ID:%d  Name:%s"), Desc.DroneId, *Desc.Name));
	}
	return Result;
}

// ---------------------------------------------------------------------------
// 通用设置
// ---------------------------------------------------------------------------



















// 替换掉原来的 SaveSettings
void UMainMenuWidget::SaveSettings(const FString& InBackendAddress, float InMapCenterLat, float InMapCenterLon, float InMapCenterAlt, bool bInUseCesium)
{
	// 1. 拼接字符串（新增 Altitude 字段，共5段）
	FString SaveString = FString::Printf(TEXT("%s|%f|%f|%f|%d"),
		*InBackendAddress, InMapCenterLat, InMapCenterLon, InMapCenterAlt, bInUseCesium ? 1 : 0);

	// 2. 路径在 Saved/Settings.txt
	FString SavePath = FPaths::ProjectSavedDir() + TEXT("Settings.txt");

	// 3. 执行写入
	bool bSuccess = FFileHelper::SaveStringToFile(SaveString, *SavePath);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("数据已保存至: %s"), *SavePath);
		OnSettingsSaved();
	}
}

// 替换掉原来的 LoadSettings
void UMainMenuWidget::LoadSettings(FString& OutBackendAddress, float& OutMapCenterLat, float& OutMapCenterLon, float& OutMapCenterAlt, bool& OutbUseCesium) const
{
	FString SavePath = FPaths::ProjectSavedDir() + TEXT("Settings.txt");
	FString LoadedString;

	if (FFileHelper::LoadFileToString(LoadedString, *SavePath))
	{
		TArray<FString> Parsed;
		LoadedString.ParseIntoArray(Parsed, TEXT("|"));
		if (Parsed.Num() >= 5)
		{
			OutBackendAddress = Parsed[0];
			OutMapCenterLat   = FCString::Atof(*Parsed[1]);
			OutMapCenterLon   = FCString::Atof(*Parsed[2]);
			OutMapCenterAlt   = FCString::Atof(*Parsed[3]);
			OutbUseCesium     = (Parsed[4] == TEXT("1"));
			UE_LOG(LogTemp, Log, TEXT("设置已读取"));
			return;
		}
		// 兼容旧格式（4段，无Altitude）
		if (Parsed.Num() == 4)
		{
			OutBackendAddress = Parsed[0];
			OutMapCenterLat   = FCString::Atof(*Parsed[1]);
			OutMapCenterLon   = FCString::Atof(*Parsed[2]);
			OutMapCenterAlt   = 43.0f; // 默认北京平均海拔
			OutbUseCesium     = (Parsed[3] == TEXT("1"));
			UE_LOG(LogTemp, Log, TEXT("设置已读取（旧格式，Altitude使用默认值43m）"));
			return;
		}
	}

	// 默认值
	OutBackendAddress = TEXT("ws://192.168.30.104:8080");
	OutMapCenterLat   = 40.0f;
	OutMapCenterLon   = 116.0f;
	OutMapCenterAlt   = 43.0f;
	OutbUseCesium     = true;
}

void UMainMenuWidget::LoadMapSettings(FDroneMapSettings& OutSettings) const
{
	UDroneMapSettingsBlueprintLibrary::LoadDroneMapSettings(OutSettings);
}

void UMainMenuWidget::SaveMapSettings(const FDroneMapSettings& Settings)
{
	UDroneMapSettingsBlueprintLibrary::SaveDroneMapSettings(Settings);
}

void UMainMenuWidget::SetOfflineMapEnabled(bool bUseOfflineMap)
{
	UDroneMapSettingsBlueprintLibrary::SetOfflineMapEnabled(this, bUseOfflineMap, false);
}

bool UMainMenuWidget::IsOfflineMapEnabled() const
{
	return UDroneMapSettingsBlueprintLibrary::IsOfflineMapEnabled();
}
// ---------------------------------------------------------------------------
// 导航
// ---------------------------------------------------------------------------

void UMainMenuWidget::OnGoToQueueEditorClicked()
{
	if (AMainMenuPlayerController* PC = GetMainMenuPC())
	{
		PC->GoToQueueEditor();
	}
}

void UMainMenuWidget::OnGoToPreviewClicked()
{
	if (!IsBackendConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Cannot enter preview: backend WebSocket not connected"));
		OnGoToPreviewBlockedByNoConnection();
		return;
	}

	if (AMainMenuPlayerController* PC = GetMainMenuPC())
	{
		PC->GoToPreview();
	}
}

bool UMainMenuWidget::IsBackendConnected() const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr || !NetMgr->GetWebSocketClient())
	{
		return false;
	}

	return NetMgr->GetWebSocketClient()->IsConnected();
}

// ---------------------------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------------------------

AMainMenuPlayerController* UMainMenuWidget::GetMainMenuPC() const
{
	return Cast<AMainMenuPlayerController>(GetOwningPlayer());
}
