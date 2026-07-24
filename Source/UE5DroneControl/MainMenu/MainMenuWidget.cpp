// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"
#include "MainMenuPlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include <fstream>
#include <string>
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"


void UMainMenuWidget::BuildIsolationControls()
{
	if (!WidgetTree)
	{
		return;
	}

	IsolationCheckBox = Cast<UCheckBox>(
		GetWidgetFromName(TEXT("LocalPreviewIsolationCheckBox")));
	IsolationStatusBanner = Cast<UBorder>(
		GetWidgetFromName(TEXT("LocalPreviewIsolationStatusBanner")));
	if (IsolationCheckBox)
	{
		return;
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		UWidget* ExistingRoot = WidgetTree->RootWidget;
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("MainMenuRuntimeRoot"));
		WidgetTree->RootWidget = RootCanvas;

		if (ExistingRoot)
		{
			if (UCanvasPanelSlot* ExistingSlot = RootCanvas->AddChildToCanvas(ExistingRoot))
			{
				ExistingSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				ExistingSlot->SetOffsets(FMargin(0.0f));
				ExistingSlot->SetZOrder(0);
			}
		}
	}

	UVerticalBox* Container = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("LocalPreviewIsolationContainer"));
	if (UCanvasPanelSlot* ContainerSlot = RootCanvas->AddChildToCanvas(Container))
	{
		ContainerSlot->SetAnchors(FAnchors(1.0f, 0.0f));
		ContainerSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		ContainerSlot->SetPosition(FVector2D(-32.0f, 16.0f));
		ContainerSlot->SetAutoSize(true);
		ContainerSlot->SetZOrder(100);
	}

	UBorder* ToggleBackground = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("LocalPreviewIsolationToggleBackground"));
	ToggleBackground->SetBrushColor(FLinearColor(0.08f, 0.08f, 0.12f, 0.94f));
	ToggleBackground->SetPadding(FMargin(12.0f, 8.0f));
	Container->AddChildToVerticalBox(ToggleBackground);

	UHorizontalBox* ToggleRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("LocalPreviewIsolationToggleRow"));
	ToggleBackground->SetContent(ToggleRow);

	IsolationCheckBox = WidgetTree->ConstructWidget<UCheckBox>(
		UCheckBox::StaticClass(), TEXT("LocalPreviewIsolationCheckBox"));
	if (UHorizontalBoxSlot* CheckSlot = ToggleRow->AddChildToHorizontalBox(IsolationCheckBox))
	{
		CheckSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		CheckSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("LocalPreviewIsolationLabel"));
	Label->SetText(FText::FromString(TEXT("纯本地预演（隔离后端）")));
	Label->SetColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f));
	FSlateFontInfo LabelFont = Label->GetFont();
	LabelFont.Size = 14;
	Label->SetFont(LabelFont);
	if (UHorizontalBoxSlot* LabelSlot = ToggleRow->AddChildToHorizontalBox(Label))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	IsolationStatusBanner = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("LocalPreviewIsolationStatusBanner"));
	IsolationStatusBanner->SetBrushColor(FLinearColor(0.85f, 0.12f, 0.12f, 0.95f));
	IsolationStatusBanner->SetPadding(FMargin(12.0f, 6.0f));
	IsolationStatusBanner->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* BannerSlot =
		Container->AddChildToVerticalBox(IsolationStatusBanner))
	{
		BannerSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	}

	UTextBlock* WarningText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("LocalPreviewIsolationWarningText"));
	WarningText->SetText(FText::FromString(
		TEXT("⚠ 后端通信已隔离 — 所有出站请求已阻止")));
	WarningText->SetColorAndOpacity(FLinearColor::White);
	FSlateFontInfo WarningFont = WarningText->GetFont();
	WarningFont.Size = 13;
	WarningText->SetFont(WarningFont);
	IsolationStatusBanner->SetContent(WarningText);
}

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildIsolationControls();
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UGameInstance* GI = GetGameInstance())
	{
		CachedNetworkManager = GI->GetSubsystem<UDroneNetworkManager>();
	}
	if (IsolationCheckBox)
	{
		IsolationCheckBox->OnCheckStateChanged.AddUniqueDynamic(
			this, &UMainMenuWidget::HandleIsolationCheckChanged);
	}
	if (CachedNetworkManager)
	{
		CachedNetworkManager->OnIsolationStateChanged.AddUniqueDynamic(
			this, &UMainMenuWidget::HandleIsolationStateChanged);
		SyncIsolationControls(CachedNetworkManager->IsStrictLocalPreviewIsolation());
	}
	// 初始化时加载已保存设置（蓝图子类可在 Construct 事件中调用 LoadSettings）
}

// ---------------------------------------------------------------------------
// 无人机注册
// ---------------------------------------------------------------------------

void UMainMenuWidget::NativeDestruct()
{
	if (CachedNetworkManager)
	{
		CachedNetworkManager->OnIsolationStateChanged.RemoveDynamic(
			this, &UMainMenuWidget::HandleIsolationStateChanged);
	}
	if (IsolationCheckBox)
	{
		IsolationCheckBox->OnCheckStateChanged.RemoveDynamic(
			this, &UMainMenuWidget::HandleIsolationCheckChanged);
	}
	CachedNetworkManager = nullptr;
	Super::NativeDestruct();
}

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
	if (!IsStrictLocalPreviewIsolationEnabled() && !IsBackendConnected())
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
// 垂直定位射线开关
// ---------------------------------------------------------------------------

void UMainMenuWidget::SetGroundProjectionRayEnabled(bool bEnabled)
{
	if (GConfig)
	{
		GConfig->SetBool(TEXT("DroneDisplay"), TEXT("bShowGroundProjectionRay"), bEnabled, GGameIni);
		GConfig->Flush(false, GGameIni);
	}
}

bool UMainMenuWidget::IsGroundProjectionRayEnabled() const
{
	bool bEnabled = true;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("DroneDisplay"), TEXT("bShowGroundProjectionRay"), bEnabled, GGameIni);
	}
	return bEnabled;
}

void UMainMenuWidget::SetStrictLocalPreviewIsolation(bool bEnabled)
{
	UDroneNetworkManager* NetMgr = CachedNetworkManager;
	if (!NetMgr)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
			CachedNetworkManager = NetMgr;
		}
	}

	if (NetMgr)
	{
		NetMgr->SetStrictLocalPreviewIsolation(bEnabled);
		SyncIsolationControls(NetMgr->IsStrictLocalPreviewIsolation());
	}
}

bool UMainMenuWidget::IsStrictLocalPreviewIsolationEnabled() const
{
	UGameInstance* GI = GetGameInstance();
	const UDroneNetworkManager* NetMgr = CachedNetworkManager
		? CachedNetworkManager.Get()
		: (GI ? GI->GetSubsystem<UDroneNetworkManager>() : nullptr);
	return NetMgr && NetMgr->IsStrictLocalPreviewIsolation();
}

void UMainMenuWidget::HandleIsolationCheckChanged(bool bIsChecked)
{
	if (!bSuppressIsolationCallback)
	{
		SetStrictLocalPreviewIsolation(bIsChecked);
	}
}

void UMainMenuWidget::HandleIsolationStateChanged(bool bIsolated)
{
	SyncIsolationControls(bIsolated);
}

void UMainMenuWidget::SyncIsolationControls(bool bIsolated)
{
	if (IsolationCheckBox)
	{
		bSuppressIsolationCallback = true;
		IsolationCheckBox->SetIsChecked(bIsolated);
		bSuppressIsolationCallback = false;
	}
	if (IsolationStatusBanner)
	{
		IsolationStatusBanner->SetVisibility(
			bIsolated ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

// ---------------------------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------------------------

AMainMenuPlayerController* UMainMenuWidget::GetMainMenuPC() const
{
	return Cast<AMainMenuPlayerController>(GetOwningPlayer());
}
