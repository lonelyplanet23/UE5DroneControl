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

	FDroneDescriptor Desc;
	Desc.DroneId   = DroneId;
	Desc.Name      = FString::Printf(TEXT("UAV-%d"), DroneId);
	// IpAddress 目前不在 FDroneDescriptor 中，如需持久化 IP 请扩展该结构体
	// 或通过 DroneNetworkManager 单独配置连接地址

	Registry->RegisterDrone(Desc);
	OnDroneRegistered(DroneId);
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
void UMainMenuWidget::SaveSettings(const FString& InBackendAddress, float InMapCenterLat, float InMapCenterLon, bool bInUseCesium)
{
	// 1. 拼接字符串
	FString SaveString = FString::Printf(TEXT("%s|%f|%f|%d"), *InBackendAddress, InMapCenterLat, InMapCenterLon, bInUseCesium ? 1 : 0);

	// 2. 路径在 Saved/Settings.txt
	FString SavePath = FPaths::ProjectSavedDir() + TEXT("Settings.txt");

	// 3. 执行写入
	bool bSuccess = FFileHelper::SaveStringToFile(SaveString, *SavePath);

	if (bSuccess)
	{
		// 如果这里成功，输出日志并在界面显示提示
		UE_LOG(LogTemp, Warning, TEXT("数据已保存至: %s"), *SavePath);
		OnSettingsSaved();
	}
}

// 替换掉原来的 LoadSettings
void UMainMenuWidget::LoadSettings(FString& OutBackendAddress, float& OutMapCenterLat, float& OutMapCenterLon, bool& OutbUseCesium) const
{
	FString SavePath = FPaths::ProjectSavedDir() + TEXT("Settings.txt");
	FString LoadedString;

	if (FFileHelper::LoadFileToString(LoadedString, *SavePath))
	{
		TArray<FString> Parsed;
		LoadedString.ParseIntoArray(Parsed, TEXT("|"));
		if (Parsed.Num() == 4)
		{
			OutBackendAddress = Parsed[0];
			OutMapCenterLat = FCString::Atof(*Parsed[1]);
			OutMapCenterLon = FCString::Atof(*Parsed[2]);
			OutbUseCesium = (Parsed[3] == TEXT("1"));
			UE_LOG(LogTemp, Log, TEXT("设置已读取"));
			return;
		}
	}

	// 默认值
	OutBackendAddress = TEXT("ws://192.168.30.104:8080");
	OutMapCenterLat = 40.0f;
	OutMapCenterLon = 116.0f;
	OutbUseCesium = true;
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
	if (AMainMenuPlayerController* PC = GetMainMenuPC())
	{
		PC->GoToPreview();
	}
}

// ---------------------------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------------------------

AMainMenuPlayerController* UMainMenuWidget::GetMainMenuPC() const
{
	return Cast<AMainMenuPlayerController>(GetOwningPlayer());
}
