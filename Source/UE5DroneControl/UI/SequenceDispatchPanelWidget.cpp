#include "SequenceDispatchPanelWidget.h"
#include "PathFileListItemWidget.h"
#include "PathDroneMatchItemWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

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
	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnBackClicked);
	}

	SetPanelState(ESequencePanelState::Collapsed);
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
			Item->SetFileInfo(FileName, FPaths::Combine(SearchDir, FileName));
			Item->OnClicked.BindUObject(this, &USequenceDispatchPanelWidget::OnFileSelected);
			FileListScrollBox->AddChild(Item);
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
	PopulateMatchingView();
	SetPanelState(ESequencePanelState::Matching);
	SetStatusMessage(FString::Printf(TEXT("已加载 %d 条路径，拖拽匹配无人机"), LoadedPathsData.Paths.Num()));
}

void USequenceDispatchPanelWidget::PopulateMatchingView()
{
	if (PathListScrollBox) PathListScrollBox->ClearChildren();
	if (DroneListScrollBox) DroneListScrollBox->ClearChildren();
	if (!MatchItemClass) return;

	for (int32 i = 0; i < LoadedPathsData.Paths.Num(); ++i)
	{
		UPathDroneMatchItemWidget* PathItem = CreateWidget<UPathDroneMatchItemWidget>(this, MatchItemClass);
		if (PathItem)
		{
			PathItem->SetAsPathItem(LoadedPathsData.Paths[i].PathId, i);
			if (PathListScrollBox) PathListScrollBox->AddChild(PathItem);
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
			DroneItem->SetAsDroneItem(Desc.DroneId, Desc.Name);
			DroneItem->OnMatchCompleted.BindUObject(this, &USequenceDispatchPanelWidget::OnMatchMade);
			if (DroneListScrollBox) DroneListScrollBox->AddChild(DroneItem);
		}
	}

	UpdateDispatchButtonState();
}

void USequenceDispatchPanelWidget::OnMatchMade(int32 PathIndex, int32 DroneId)
{
	// Remove any existing match for this drone
	for (auto It = MatchedPairs.CreateIterator(); It; ++It)
	{
		if (It->Value == DroneId)
		{
			It.RemoveCurrent();
			break;
		}
	}

	MatchedPairs.Add(PathIndex, DroneId);

	// Update visual state on path items
	if (PathListScrollBox)
	{
		for (int32 i = 0; i < PathListScrollBox->GetChildrenCount(); ++i)
		{
			UPathDroneMatchItemWidget* Item = Cast<UPathDroneMatchItemWidget>(PathListScrollBox->GetChildAt(i));
			if (!Item) continue;
			if (const int32* Matched = MatchedPairs.Find(Item->GetPathIndex()))
			{
				Item->SetMatchedLabel(FString::Printf(TEXT("-> Drone %d"), *Matched));
			}
			else
			{
				Item->ClearMatch();
			}
		}
	}

	// Update visual state on drone items
	if (DroneListScrollBox)
	{
		for (int32 i = 0; i < DroneListScrollBox->GetChildrenCount(); ++i)
		{
			UPathDroneMatchItemWidget* Item = Cast<UPathDroneMatchItemWidget>(DroneListScrollBox->GetChildAt(i));
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

	UpdateDispatchButtonState();
	SetStatusMessage(FString::Printf(TEXT("已匹配 %d / %d"), MatchedPairs.Num(), LoadedPathsData.Paths.Num()));
}

void USequenceDispatchPanelWidget::UpdateDispatchButtonState()
{
	if (!DispatchButton) return;
	const bool bAllMatched = MatchedPairs.Num() == LoadedPathsData.Paths.Num() && !LoadedPathsData.Paths.IsEmpty();
	DispatchButton->SetIsEnabled(bAllMatched);
}

void USequenceDispatchPanelWidget::OnDispatchClicked()
{
	if (MatchedPairs.Num() != LoadedPathsData.Paths.Num())
	{
		SetStatusMessage(TEXT("请先完成所有路径匹配"));
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	UGameInstance* GI = PC->GetGameInstance();
	if (!GI) return;

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr) return;

	TMap<int32, FDronePathSaveData> DispatchMap;
	for (const auto& Pair : MatchedPairs)
	{
		if (LoadedPathsData.Paths.IsValidIndex(Pair.Key))
		{
			DispatchMap.Add(Pair.Value, LoadedPathsData.Paths[Pair.Key]);
		}
	}

	FOnHttpResponse Callback;
	Callback.BindDynamic(this, &USequenceDispatchPanelWidget::OnDispatchResponse);
	NetMgr->SendArrayTaskFromData(DispatchMap, Callback);

	SetStatusMessage(TEXT("正在派发..."));
	if (DispatchButton) DispatchButton->SetIsEnabled(false);
}

void USequenceDispatchPanelWidget::OnDispatchResponse(bool bSuccess, const FString& ResponseBody)
{
	if (bSuccess)
	{
		SetStatusMessage(TEXT("派发成功"));
		MatchedPairs.Empty();
		SetPanelState(ESequencePanelState::Collapsed);
	}
	else
	{
		SetStatusMessage(FString::Printf(TEXT("派发失败: %s"), *ResponseBody));
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
