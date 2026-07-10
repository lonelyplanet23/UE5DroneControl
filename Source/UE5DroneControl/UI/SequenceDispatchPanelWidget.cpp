#include "SequenceDispatchPanelWidget.h"
#include "PathFileListItemWidget.h"
#include "PathDroneMatchItemWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CheckBox.h"
#include "RealTimeDroneReceiver.h"
#include "MultiDroneCharacter.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOps/Control/DroneOpsPlayerController.h"
#include "PathEditor/DronePathActor.h"
#include "PreviewConfirmPopupWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
UPathDroneMatchItemWidget* GetMatchItemFromScrollChild(UWidget* Child)
{
	if (UPathDroneMatchItemWidget* Item = Cast<UPathDroneMatchItemWidget>(Child))
	{
		return Item;
	}

	if (USizeBox* SizeBox = Cast<USizeBox>(Child))
	{
		return Cast<UPathDroneMatchItemWidget>(SizeBox->GetContent());
	}

	return nullptr;
}
}

bool USequenceDispatchPanelWidget::bStaticPanelInteractive = false;

bool USequenceDispatchPanelWidget::IsPanelInteractive()
{
	return bStaticPanelInteractive;
}

void USequenceDispatchPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (OpenButton)
	{
		OpenButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnOpenButtonClicked);
	}
	if (DispatchButton)
	{
		DispatchButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnDispatchClicked);
	}
	if (LocalPreviewButton)
	{
		LocalPreviewButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnLocalPreviewClicked);
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnBackClicked);
	}
	if (AutoAssignCheckBox)
	{
		AutoAssignCheckBox->OnCheckStateChanged.AddDynamic(this, &USequenceDispatchPanelWidget::OnAutoAssignCheckChanged);
		bAutoAssignEnabled = AutoAssignCheckBox->IsChecked();
	}
	if (PathEditToggle)
	{
		PathEditToggle->OnCheckStateChanged.AddDynamic(this, &USequenceDispatchPanelWidget::OnPathEditToggleChanged);
	}
	if (SavePathButton)
	{
		SavePathButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnSavePathClicked);
		SavePathButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	PopulateModeComboBox();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
		{
			AssignmentResultHandle = NetMgr->OnAssignmentResult.AddUObject(
				this, &USequenceDispatchPanelWidget::HandleAssignmentResult);
		}
	}

	SetPanelState(ESequencePanelState::Collapsed);
}

void USequenceDispatchPanelWidget::NativeDestruct()
{
	// 编辑中被销毁：清理临时路径，避免残留
	if (bInPathEditMode)
	{
		if (ADroneOpsPlayerController* PC = GetDroneOpsController())
		{
			PC->ClearEditingPaths();
		}
		bInPathEditMode = false;
	}

	if (AssignmentResultHandle.IsValid())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
			{
				NetMgr->OnAssignmentResult.Remove(AssignmentResultHandle);
			}
		}
		AssignmentResultHandle.Reset();
	}

	Super::NativeDestruct();
}

void USequenceDispatchPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	const bool bCursorVisible = PC->bShowMouseCursor;
	const ESlateVisibility DesiredVisibility = bCursorVisible
		? ESlateVisibility::Visible
		: ESlateVisibility::HitTestInvisible;

	if (GetVisibility() != DesiredVisibility)
	{
		SetVisibility(DesiredVisibility);
	}
}

void USequenceDispatchPanelWidget::SetPanelState(ESequencePanelState NewState)
{
	CurrentState = NewState;
	bStaticPanelInteractive = (NewState != ESequencePanelState::Collapsed);
	if (NewState != ESequencePanelState::Matching)
	{
		SelectedPathIndex = INDEX_NONE;
	}

	if (FileListPanel)
	{
		FileListPanel->SetVisibility(NewState == ESequencePanelState::FileList
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (MatchingPanel)
	{
		MatchingPanel->SetVisibility(NewState == ESequencePanelState::Matching
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (DispatchButton)
	{
		DispatchButton->SetVisibility(NewState == ESequencePanelState::Matching
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (LocalPreviewButton)
	{
		LocalPreviewButton->SetVisibility(NewState == ESequencePanelState::Matching
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (BackButton)
	{
		BackButton->SetVisibility(NewState != ESequencePanelState::Collapsed
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void USequenceDispatchPanelWidget::OnOpenButtonClicked()
{
	if (CurrentState == ESequencePanelState::Collapsed)
	{
		ScanDronePathFiles();
		SetPanelState(ESequencePanelState::FileList);
	}
	else
	{
		SetPanelState(ESequencePanelState::Collapsed);
	}
}

void USequenceDispatchPanelWidget::OnBackClicked()
{
	if (CurrentState == ESequencePanelState::Matching)
	{
		MatchedPairs.Empty();
		SelectedPathIndex = INDEX_NONE;
		SetPanelState(ESequencePanelState::FileList);
		ScanDronePathFiles();
	}
	else
	{
		SetPanelState(ESequencePanelState::Collapsed);
	}
}

void USequenceDispatchPanelWidget::ScanDronePathFiles()
{
	if (!FileListScrollBox) return;
	FileListScrollBox->ClearChildren();

	const FString SearchDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DronePaths"));
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *FPaths::Combine(SearchDir, TEXT("*.json")), true, false);

	if (FoundFiles.IsEmpty())
	{
		SetStatusMessage(TEXT("DronePaths 文件夹为空"));
		return;
	}

	SetStatusMessage(FString::Printf(TEXT("找到 %d 个文件"), FoundFiles.Num()));

	for (const FString& FileName : FoundFiles)
	{
		if (!FileListItemClass) continue;

		UPathFileListItemWidget* Item = CreateWidget<UPathFileListItemWidget>(this, FileListItemClass);
		if (Item)
		{
			USizeBox* SizeBox = NewObject<USizeBox>(FileListScrollBox);
			SizeBox->SetMinDesiredHeight(40.f);
			SizeBox->AddChild(Item);
			FileListScrollBox->AddChild(SizeBox);
			Item->SetFileInfo(FileName, FPaths::Combine(SearchDir, FileName));
			Item->OnClicked.BindUObject(this, &USequenceDispatchPanelWidget::OnFileSelected);
		}
	}
}

void USequenceDispatchPanelWidget::OnFileSelected(const FString& FilePath)
{
	LoadedPathsData = FDronePathsSaveData();
	if (!UDronePathSaveLibrary::LoadPathsFromJson(FilePath, LoadedPathsData))
	{
		SetStatusMessage(TEXT("加载文件失败"));
		return;
	}

	if (LoadedPathsData.Paths.IsEmpty())
	{
		SetStatusMessage(TEXT("文件中无有效路径"));
		return;
	}

	MatchedPairs.Empty();
	SelectedPathIndex = INDEX_NONE;
	PopulateMatchingView();
	SetPanelState(ESequencePanelState::Matching);
	SetStatusMessage(FString::Printf(TEXT("已加载 %d 条路径，先点击 Path，再点击无人机分配"), LoadedPathsData.Paths.Num()));
}

void USequenceDispatchPanelWidget::PopulateMatchingView()
{
	if (PathListScrollBox) PathListScrollBox->ClearChildren();
	if (DroneListScrollBox) DroneListScrollBox->ClearChildren();
	if (!PathListScrollBox || !DroneListScrollBox || !MatchItemClass) return;

	for (int32 i = 0; i < LoadedPathsData.Paths.Num(); ++i)
	{
		UPathDroneMatchItemWidget* PathItem = CreateWidget<UPathDroneMatchItemWidget>(this, MatchItemClass);
		if (PathItem)
		{
			USizeBox* SizeBox = NewObject<USizeBox>(PathListScrollBox);
			SizeBox->SetMinDesiredHeight(44.f);
			SizeBox->AddChild(PathItem);
			if (PathListScrollBox) PathListScrollBox->AddChild(SizeBox);
			PathItem->SetAsPathItem(LoadedPathsData.Paths[i].PathId, i);
			PathItem->OnItemClicked.BindUObject(this, &USequenceDispatchPanelWidget::OnItemClicked);
		}
	}

	UGameInstance* GI = GetOwningPlayer() ? GetOwningPlayer()->GetGameInstance() : nullptr;
	if (!GI) return;

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry) return;

	TArray<FDroneDescriptor> Descriptors = Registry->GetAllDroneDescriptors();
	for (const FDroneDescriptor& Desc : Descriptors)
	{
		UPathDroneMatchItemWidget* DroneItem = CreateWidget<UPathDroneMatchItemWidget>(this, MatchItemClass);
		if (DroneItem)
		{
			USizeBox* SizeBox = NewObject<USizeBox>(DroneListScrollBox);
			SizeBox->SetMinDesiredHeight(44.f);
			SizeBox->AddChild(DroneItem);
			if (DroneListScrollBox) DroneListScrollBox->AddChild(SizeBox);
			DroneItem->SetAsDroneItem(Desc.DroneId, Desc.BackendIdString);
			DroneItem->OnItemClicked.BindUObject(this, &USequenceDispatchPanelWidget::OnItemClicked);
		}
	}

	UpdateMatchVisuals();
	UpdateDispatchButtonState();
}

void USequenceDispatchPanelWidget::PopulateModeComboBox()
{
	if (!ModeComboBox)
	{
		return;
	}

	ModeComboBox->ClearOptions();
	ModeComboBox->AddOption(TEXT("scout"));
	ModeComboBox->AddOption(TEXT("patrol"));
	ModeComboBox->AddOption(TEXT("attack"));
	ModeComboBox->SetSelectedOption(DroneCommandModeToProtocolString(DispatchMode));
	ModeComboBox->OnSelectionChanged.AddDynamic(this, &USequenceDispatchPanelWidget::OnModeSelectionChanged);
}

void USequenceDispatchPanelWidget::OnItemClicked(bool bIsPath, int32 IndexOrId)
{
	if (bAutoAssignEnabled)
	{
		SetStatusMessage(TEXT("自动分配已开启，无需手动匹配（取消勾选可手动指定）"));
		return;
	}

	if (bIsPath)
	{
		if (!LoadedPathsData.Paths.IsValidIndex(IndexOrId))
		{
			return;
		}

		SelectedPathIndex = (SelectedPathIndex == IndexOrId) ? INDEX_NONE : IndexOrId;
		UpdateMatchVisuals();

		if (SelectedPathIndex == INDEX_NONE)
		{
			SetStatusMessage(TEXT("已取消选择 Path"));
			return;
		}

		const int32 PathId = LoadedPathsData.Paths[SelectedPathIndex].PathId;
		SetStatusMessage(FString::Printf(TEXT("已选择 Path %d，请点击无人机分配"), PathId));
		return;
	}

	if (!LoadedPathsData.Paths.IsValidIndex(SelectedPathIndex))
	{
		SetStatusMessage(TEXT("请先点击一个 Path，再点击无人机分配"));
		return;
	}

	const int32 PathIndex = SelectedPathIndex;
	const int32 DroneId = IndexOrId;

	for (auto It = MatchedPairs.CreateIterator(); It; ++It)
	{
		if (It->Value == DroneId)
		{
			It.RemoveCurrent();
			break;
		}
	}

	MatchedPairs.Add(PathIndex, DroneId);
	SelectedPathIndex = INDEX_NONE;

	UpdateMatchVisuals();
	UpdateDispatchButtonState();
	SetStatusMessage(FString::Printf(TEXT("已匹配 %d / %d"), MatchedPairs.Num(), LoadedPathsData.Paths.Num()));
}

void USequenceDispatchPanelWidget::UpdateMatchVisuals()
{
	if (PathListScrollBox)
	{
		for (int32 i = 0; i < PathListScrollBox->GetChildrenCount(); ++i)
		{
			UPathDroneMatchItemWidget* Item = GetMatchItemFromScrollChild(PathListScrollBox->GetChildAt(i));
			if (!Item) continue;
			if (const int32* Matched = MatchedPairs.Find(Item->GetPathIndex()))
			{
				Item->SetMatchedLabel(FString::Printf(TEXT("-> d%d"), *Matched));
			}
			else
			{
				Item->ClearMatch();
			}

			if (Item->GetPathIndex() == SelectedPathIndex)
			{
				Item->SetSelected(true);
			}
		}
	}

	if (DroneListScrollBox)
	{
		for (int32 i = 0; i < DroneListScrollBox->GetChildrenCount(); ++i)
		{
			UPathDroneMatchItemWidget* Item = GetMatchItemFromScrollChild(DroneListScrollBox->GetChildAt(i));
			if (!Item) continue;
			bool bMatched = false;
			for (const auto& Pair : MatchedPairs)
			{
				if (Pair.Value == Item->GetItemId())
				{
					Item->SetMatchedLabel(FString::Printf(TEXT("<- Path %d"), LoadedPathsData.Paths[Pair.Key].PathId));
					bMatched = true;
					break;
				}
			}
			if (!bMatched) Item->ClearMatch();
		}
	}
}

void USequenceDispatchPanelWidget::UpdateDispatchButtonState()
{
	if (!DispatchButton) return;
	const bool bAllMatched = MatchedPairs.Num() == LoadedPathsData.Paths.Num() && !LoadedPathsData.Paths.IsEmpty();
	const bool bAutoReady = bAutoAssignEnabled && !LoadedPathsData.Paths.IsEmpty();
	DispatchButton->SetIsEnabled(bAllMatched || bAutoReady);
}

void USequenceDispatchPanelWidget::OnAutoAssignCheckChanged(bool bIsChecked)
{
	bAutoAssignEnabled = bIsChecked;
	SelectedPathIndex = INDEX_NONE;

	if (bIsChecked)
	{
		MatchedPairs.Empty();
		SetStatusMessage(TEXT("自动分配已开启：派发后由后端匈牙利算法分配无人机"));
	}
	else
	{
		SetStatusMessage(TEXT("自动分配已关闭：请手动匹配路径与无人机"));
	}

	UpdateMatchVisuals();
	UpdateDispatchButtonState();
}

void USequenceDispatchPanelWidget::OnDispatchClicked()
{
	if (!bAutoAssignEnabled && MatchedPairs.Num() != LoadedPathsData.Paths.Num())
	{
		SetStatusMessage(TEXT("请先完成所有路径匹配"));
		return;
	}

	if (LoadedPathsData.Paths.IsEmpty())
	{
		SetStatusMessage(TEXT("请先加载路径文件"));
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	UGameInstance* GI = PC->GetGameInstance();
	if (!GI) return;

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr) return;

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry) return;

	if (bAutoAssignEnabled)
	{
		DispatchWithAutoAssign(NetMgr, Registry);
		return;
	}

	// Build DroneId -> PathSaveData map
	TMap<int32, FDronePathSaveData> DispatchMap;
	for (const auto& Pair : MatchedPairs)
	{
		if (LoadedPathsData.Paths.IsValidIndex(Pair.Key))
		{
			DispatchMap.Add(Pair.Value, LoadedPathsData.Paths[Pair.Key]);
		}
	}

	// Find reference drone: lowest DroneId among matched drones
	int32 RefDroneId = MAX_int32;
	for (const auto& Pair : DispatchMap)
	{
		if (Pair.Key < RefDroneId)
		{
			RefDroneId = Pair.Key;
		}
	}

	// Get reference drone's GPS anchor (UE world location, cm)
	FVector RefAnchor = FVector::ZeroVector;
	bool bHasAnchor = false;
	if (AActor* ReceiverActor = Registry->GetReceiverActor(RefDroneId))
	{
		if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(ReceiverActor))
		{
			if (Receiver->bHasGpsAnchor)
			{
				RefAnchor = Receiver->AnchorWorldLocation;
				bHasAnchor = true;
			}
		}
	}

	if (!bHasAnchor)
	{
		SetStatusMessage(FString::Printf(TEXT("参考机 DroneId=%d 尚未获得GPS锚点，请等待上电信息"), RefDroneId));
		return;
	}

	// Get reference path's first waypoint as the coordinate origin in edit-map space
	const FDronePathSaveData& RefPath = DispatchMap[RefDroneId];
	if (RefPath.Waypoints.IsEmpty())
	{
		SetStatusMessage(TEXT("参考机路径没有航点"));
		return;
	}
	const FVector RefEditOrigin = RefPath.Waypoints[0].Location;

	// Remap all waypoints: offset relative to ref path origin, then add ref anchor
	TMap<int32, FDronePathSaveData> RemappedMap;
	for (auto& Pair : DispatchMap)
	{
		FDronePathSaveData Remapped = Pair.Value;
		for (FDroneWaypointSaveData& Wp : Remapped.Waypoints)
		{
			// (EditMap_Waypoint - EditMap_RefOrigin) gives shape-relative offset
			// Add RefAnchor to place in Cesium world space
			Wp.Location = (Wp.Location - RefEditOrigin) + RefAnchor;
		}
		RemappedMap.Add(Pair.Key, Remapped);
	}

	FOnHttpResponse Callback;
	Callback.BindDynamic(this, &USequenceDispatchPanelWidget::OnDispatchResponse);
	PendingRemappedMap = RemappedMap;
	NetMgr->SendArrayTaskFromData(RemappedMap, DispatchMode, Callback);

	SetStatusMessage(FString::Printf(TEXT("正在派发... 参考机DroneId=%d  mode=%s"),
		RefDroneId, *DroneCommandModeToProtocolString(DispatchMode)));
	if (DispatchButton) DispatchButton->SetIsEnabled(false);
}

void USequenceDispatchPanelWidget::DispatchWithAutoAssign(UDroneNetworkManager* NetMgr, UDroneRegistrySubsystem* Registry)
{
	// 参考锚点：编号最小且已有 GPS 锚点的无人机（与手动模式的参考机规则一致）
	int32 RefDroneId = MAX_int32;
	FVector RefAnchor = FVector::ZeroVector;
	bool bHasAnchor = false;
	for (const FDroneDescriptor& Desc : Registry->GetAllDroneDescriptors())
	{
		if (Desc.DroneId >= RefDroneId) continue;
		if (AActor* ReceiverActor = Registry->GetReceiverActor(Desc.DroneId))
		{
			if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(ReceiverActor))
			{
				if (Receiver->bHasGpsAnchor)
				{
					RefDroneId = Desc.DroneId;
					RefAnchor = Receiver->AnchorWorldLocation;
					bHasAnchor = true;
				}
			}
		}
	}

	if (!bHasAnchor)
	{
		SetStatusMessage(TEXT("没有无人机获得GPS锚点，无法自动分配派发"));
		return;
	}

	// 以第一条路径的首航点为编辑系原点，整体平移到参考机锚点（与手动模式的重映射一致）
	const FDronePathSaveData& RefPath = LoadedPathsData.Paths[0];
	if (RefPath.Waypoints.IsEmpty())
	{
		SetStatusMessage(TEXT("第一条路径没有航点"));
		return;
	}
	const FVector RefEditOrigin = RefPath.Waypoints[0].Location;

	// PathId -> 重映射后的路径；drone_id 由后端分配
	TMap<int32, FDronePathSaveData> RemappedByPathId;
	for (const FDronePathSaveData& Path : LoadedPathsData.Paths)
	{
		FDronePathSaveData Remapped = Path;
		for (FDroneWaypointSaveData& Wp : Remapped.Waypoints)
		{
			Wp.Location = (Wp.Location - RefEditOrigin) + RefAnchor;
		}
		RemappedByPathId.Add(Path.PathId, Remapped);
	}

	FOnHttpResponse Callback;
	Callback.BindDynamic(this, &USequenceDispatchPanelWidget::OnDispatchResponse);
	PendingRemappedMap.Empty();
	PendingAutoPathsByPathId = RemappedByPathId;
	NetMgr->SendArrayTaskFromData(RemappedByPathId, DispatchMode, Callback, /*bAutoAssign=*/true);

	SetStatusMessage(FString::Printf(TEXT("正在派发（自动分配）... 参考锚点DroneId=%d  mode=%s"),
		RefDroneId, *DroneCommandModeToProtocolString(DispatchMode)));
	if (DispatchButton) DispatchButton->SetIsEnabled(false);
}

void USequenceDispatchPanelWidget::HandleAssignmentResult(const FString& ArrayId, const TArray<FDronePathAssignment>& Assignments)
{
	if (PendingAutoPathsByPathId.IsEmpty())
	{
		return; // 不是本面板发起的自动分配任务
	}

	// 按分配结果把 PathId -> PathData 转成 DroneId -> PathData，并同步槽位显示
	TMap<int32, FDronePathSaveData> ByDrone;
	MatchedPairs.Empty();
	for (const FDronePathAssignment& Assignment : Assignments)
	{
		const FDronePathSaveData* Path = PendingAutoPathsByPathId.Find(Assignment.PathId);
		if (!Path) continue;

		ByDrone.Add(Assignment.DroneId, *Path);

		for (int32 i = 0; i < LoadedPathsData.Paths.Num(); ++i)
		{
			if (LoadedPathsData.Paths[i].PathId == Assignment.PathId)
			{
				MatchedPairs.Add(i, Assignment.DroneId);
				break;
			}
		}
	}
	PendingAutoPathsByPathId.Empty();

	UpdateMatchVisuals();
	SetStatusMessage(FString::Printf(TEXT("自动分配完成（%s）：%d 条路径已分配"), *ArrayId, ByDrone.Num()));

	UE_LOG(LogTemp, Log, TEXT("[SequenceDispatch] Assignment result applied: array=%s, %d paths"),
		*ArrayId, ByDrone.Num());

	// 按分配结果驱动影子机本地播放
	PendingRemappedMap = ByDrone;
	StartShadowDronePlayback();
	UpdateDispatchButtonState();
}

void USequenceDispatchPanelWidget::OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	DispatchMode = DroneCommandModeFromProtocolString(SelectedItem);
}

void USequenceDispatchPanelWidget::OnDispatchResponse(bool bSuccess, const FString& ResponseBody)
{
	if (bSuccess)
	{
		if (bAutoAssignEnabled)
		{
			// 面板保持打开，等待 assignment_result 推送后同步槽位显示并启动影子机
			SetStatusMessage(TEXT("派发成功，等待后端自动分配结果..."));
		}
		else
		{
			SetStatusMessage(TEXT("派发成功"));
			StartShadowDronePlayback();
			MatchedPairs.Empty();
			SetPanelState(ESequencePanelState::Collapsed);
		}
	}
	else
	{
		SetStatusMessage(FString::Printf(TEXT("派发失败: %s"), *ResponseBody));
		PendingRemappedMap.Empty();
		PendingAutoPathsByPathId.Empty();
		if (DispatchButton) DispatchButton->SetIsEnabled(true);
	}
}

void USequenceDispatchPanelWidget::SetStatusMessage(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

void USequenceDispatchPanelWidget::OnLocalPreviewClicked()
{
	if (MatchedPairs.Num() != LoadedPathsData.Paths.Num())
	{
		SetStatusMessage(TEXT("请先完成所有路径匹配"));
		return;
	}

	// 不发送后端请求，直接用匹配结果驱动影子机本地播放
	TMap<int32, FDronePathSaveData> PreviewMap;
	for (const auto& Pair : MatchedPairs)
	{
		if (LoadedPathsData.Paths.IsValidIndex(Pair.Key))
		{
			PreviewMap.Add(Pair.Value, LoadedPathsData.Paths[Pair.Key]);
		}
	}

	PendingRemappedMap = PreviewMap;
	StartShadowDronePlayback();

	SetStatusMessage(TEXT("本地预演已开始（未向后端发送指令）"));
	MatchedPairs.Empty();
	SetPanelState(ESequencePanelState::Collapsed);
}

void USequenceDispatchPanelWidget::ClearActiveDispatchPaths()
{
	for (ADronePathActor* PathActor : ActiveDispatchPathActors)
	{
		if (IsValid(PathActor))
		{
			PathActor->StopMovement();
			PathActor->Destroy();
		}
	}
	ActiveDispatchPathActors.Empty();
}

void USequenceDispatchPanelWidget::StartShadowDronePlayback()
{
	ClearActiveDispatchPaths();

	if (PendingRemappedMap.IsEmpty())
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	UWorld* World = PC->GetWorld();
	if (!World) return;

	UGameInstance* GI = PC->GetGameInstance();
	if (!GI) return;

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry) return;

	for (const TPair<int32, FDronePathSaveData>& Pair : PendingRemappedMap)
	{
		const int32 DroneId = Pair.Key;
		const FDronePathSaveData& PathData = Pair.Value;

		if (PathData.Waypoints.IsEmpty()) continue;

		// 找到对应影子机（SenderPawn）
		APawn* ShadowPawn = Registry->GetSenderPawn(DroneId);
		if (!IsValid(ShadowPawn)) continue;

		// 停止跟随镜像机，否则 Tick 会每帧把影子机拉回镜像机位置
		if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ShadowPawn))
		{
			Shadow->bFollowingMirror = false;
		}

		// Spawn DronePathActor
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADronePathActor* PathActor = World->SpawnActor<ADronePathActor>(
			ADronePathActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!IsValid(PathActor)) continue;

		PathActor->SetPathNumericId(PathData.PathId);
		PathActor->bClosedLoop = PathData.bClosedLoop;

		// segmentSpeed=0 表示"使用默认速度"，DronePathActor 需要 >0 的速度才能正常计算 duration
		constexpr float DefaultSpeedMps = 5.0f;

		for (int32 i = 0; i < PathData.Waypoints.Num(); ++i)
		{
			const FDroneWaypointSaveData& WpData = PathData.Waypoints[i];
			const float EffectiveSpeed = (i == 0) ? 0.0f : (WpData.SegmentSpeed > KINDA_SMALL_NUMBER ? WpData.SegmentSpeed : DefaultSpeedMps);
			const int32 NewIdx = PathActor->AddWaypoint(WpData.Location, EffectiveSpeed);
			if (PathActor->Waypoints.IsValidIndex(NewIdx))
			{
				PathActor->Waypoints[NewIdx].WaitTime = WpData.WaitTime;
				PathActor->Waypoints[NewIdx].SegmentSpeed = EffectiveSpeed;
			}
		}

		PathActor->RefreshPath();
		PathActor->StartMovement(ShadowPawn);

		ActiveDispatchPathActors.Add(PathActor);

		UE_LOG(LogTemp, Log, TEXT("[SequenceDispatch] Shadow drone DroneId=%d started playback along path %d"),
			DroneId, PathData.PathId);
	}

	PendingRemappedMap.Empty();
}

// ================= 路径编辑模式 =================

ADroneOpsPlayerController* USequenceDispatchPanelWidget::GetDroneOpsController() const
{
	return Cast<ADroneOpsPlayerController>(GetOwningPlayer());
}

void USequenceDispatchPanelWidget::OnPathEditToggleChanged(bool bIsChecked)
{
	if (bIsChecked)
	{
		// 收集当前选中无人机：优先多选，否则主选
		TArray<int32> DroneIds;
		UGameInstance* GI = GetGameInstance();
		UDroneRegistrySubsystem* Registry = GI ? GI->GetSubsystem<UDroneRegistrySubsystem>() : nullptr;
		if (Registry)
		{
			DroneIds = Registry->GetMultiSelectedDrones();
			if (DroneIds.IsEmpty())
			{
				const int32 Primary = Registry->GetPrimarySelectedDrone();
				if (Primary > 0)
				{
					DroneIds.Add(Primary);
				}
			}
		}

		ADroneOpsPlayerController* PC = GetDroneOpsController();
		if (!PC || DroneIds.IsEmpty() || !PC->BeginPathEditMode(DroneIds))
		{
			SetStatusMessage(TEXT("请先选择无人机再进入编辑模式"));
			// 复位 Toggle（避免递归：SetIsChecked 不触发 OnCheckStateChanged）
			if (PathEditToggle)
			{
				PathEditToggle->SetIsChecked(false);
			}
			return;
		}

		bInPathEditMode = true;
		if (SavePathButton)
		{
			SavePathButton->SetVisibility(ESlateVisibility::Visible);
		}
		SetStatusMessage(FString::Printf(TEXT("编辑模式：%d 架无人机，点击地图加航点，拖拽 gizmo 微调"), DroneIds.Num()));
	}
	else
	{
		if (!bInPathEditMode)
		{
			return;
		}

		bInPathEditMode = false;
		if (SavePathButton)
		{
			SavePathButton->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (ADroneOpsPlayerController* PC = GetDroneOpsController())
		{
			PC->EndPathEditMode();
		}

		ShowPreviewConfirmPopup();
	}
}

void USequenceDispatchPanelWidget::OnSavePathClicked()
{
	ADroneOpsPlayerController* PC = GetDroneOpsController();
	if (!PC)
	{
		return;
	}

	SavedPreviewData = PC->BuildEditingPathsData();
	bHasSavedPreviewData = !SavedPreviewData.IsEmpty();
	SetStatusMessage(bHasSavedPreviewData
		? FString::Printf(TEXT("已保存 %d 条路径到内存"), SavedPreviewData.Num())
		: TEXT("没有可保存的航点"));
}

TMap<int32, FDronePathSaveData> USequenceDispatchPanelWidget::ResolvePreviewDispatchMap()
{
	if (bHasSavedPreviewData && !SavedPreviewData.IsEmpty())
	{
		return SavedPreviewData;
	}

	// 未点保存：实时从当前临时路径构建
	if (ADroneOpsPlayerController* PC = GetDroneOpsController())
	{
		return PC->BuildEditingPathsData();
	}

	return TMap<int32, FDronePathSaveData>();
}

void USequenceDispatchPanelWidget::ShowPreviewConfirmPopup()
{
	if (!PreviewConfirmPopupClass)
	{
		// 没配置弹窗类：默认取消并清理，避免临时路径残留
		SetStatusMessage(TEXT("未配置确认弹窗，已取消并清理临时路径"));
		if (ADroneOpsPlayerController* PC = GetDroneOpsController())
		{
			PC->ClearEditingPaths();
		}
		ResetPathEditState();
		return;
	}

	UPreviewConfirmPopupWidget* Popup = CreateWidget<UPreviewConfirmPopupWidget>(GetOwningPlayer(), PreviewConfirmPopupClass);
	if (!Popup)
	{
		return;
	}

	Popup->OnChoiceMade.AddDynamic(this, &USequenceDispatchPanelWidget::OnPreviewConfirmChoice);
	Popup->AddToViewport(100);
}

void USequenceDispatchPanelWidget::OnPreviewConfirmChoice(EPreviewConfirmChoice Choice)
{
	ADroneOpsPlayerController* PC = GetDroneOpsController();

	switch (Choice)
	{
	case EPreviewConfirmChoice::LocalPreview:
	{
		TMap<int32, FDronePathSaveData> Map = ResolvePreviewDispatchMap();
		// 先销毁编辑用临时路径，再用数据驱动影子机
		if (PC)
		{
			PC->ClearEditingPaths();
		}
		if (Map.IsEmpty())
		{
			SetStatusMessage(TEXT("没有可预演的路径"));
			break;
		}
		PendingRemappedMap = Map;
		StartShadowDronePlayback();
		SetStatusMessage(TEXT("本地预演已开始（未向后端发送指令）"));
		break;
	}
	case EPreviewConfirmChoice::Dispatch:
	{
		TMap<int32, FDronePathSaveData> Map = ResolvePreviewDispatchMap();
		if (PC)
		{
			PC->ClearEditingPaths();
		}
		if (Map.IsEmpty())
		{
			SetStatusMessage(TEXT("没有可派发的路径"));
			break;
		}

		UGameInstance* GI = GetGameInstance();
		UDroneNetworkManager* NetMgr = GI ? GI->GetSubsystem<UDroneNetworkManager>() : nullptr;
		if (!NetMgr)
		{
			SetStatusMessage(TEXT("网络管理器不可用，派发失败"));
			break;
		}

		// 临时路径已是预演世界坐标，直接发送，不做 anchor 重映射
		FOnHttpResponse Callback;
		Callback.BindDynamic(this, &USequenceDispatchPanelWidget::OnEditDispatchResponse);
		PendingRemappedMap = Map;
		NetMgr->SendArrayTaskFromData(Map, DispatchMode, Callback, /*bAutoAssign=*/false);
		SetStatusMessage(FString::Printf(TEXT("正在派发 %d 条路径... mode=%s"),
			Map.Num(), *DroneCommandModeToProtocolString(DispatchMode)));
		break;
	}
	case EPreviewConfirmChoice::Cancel:
	default:
	{
		if (PC)
		{
			PC->ClearEditingPaths();
		}
		SetStatusMessage(TEXT("已取消，临时路径已清理"));
		break;
	}
	}

	ResetPathEditState();
}

void USequenceDispatchPanelWidget::ResetPathEditState()
{
	bInPathEditMode = false;
	bHasSavedPreviewData = false;
	SavedPreviewData.Empty();
	if (SavePathButton)
	{
		SavePathButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (PathEditToggle && PathEditToggle->IsChecked())
	{
		PathEditToggle->SetIsChecked(false);
	}
}

void USequenceDispatchPanelWidget::OnEditDispatchResponse(bool bSuccess, const FString& ResponseBody)
{
	if (bSuccess)
	{
		SetStatusMessage(TEXT("派发成功，开始本地预演"));
		StartShadowDronePlayback();
	}
	else
	{
		SetStatusMessage(FString::Printf(TEXT("派发失败: %s"), *ResponseBody));
		PendingRemappedMap.Empty();
	}
}
