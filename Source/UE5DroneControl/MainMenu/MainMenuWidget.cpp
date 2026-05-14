// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"
#include "MainMenuPlayerController.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Kismet/GameplayStatics.h"

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

void UMainMenuWidget::SaveSettings(const FString& BackendAddress, float MapCenterLat, float MapCenterLon, bool bUseCesium)
{
	// TODO: 将设置存入自定义 GameInstance 或 USaveGame 对象
	// 当前以 UE_LOG 占位，待 GameInstance 扩展后替换
	UE_LOG(LogTemp, Log, TEXT("MainMenu SaveSettings: Backend=%s Lat=%.6f Lon=%.6f Cesium=%d"),
		*BackendAddress, MapCenterLat, MapCenterLon, bUseCesium ? 1 : 0);

	OnSettingsSaved();
}

void UMainMenuWidget::LoadSettings(FString& OutBackendAddress, float& OutMapCenterLat, float& OutMapCenterLon, bool& OutbUseCesium) const
{
	// TODO: 从自定义 GameInstance 或 USaveGame 读取
	// 当前返回默认值占位
	OutBackendAddress = TEXT("ws://192.168.30.104:8080");
	OutMapCenterLat   = 40.0f;
	OutMapCenterLon   = 116.0f;
	OutbUseCesium     = true;
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
