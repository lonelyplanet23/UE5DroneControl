#include "SequenceDispatchPanelWidget.h"
#include "PathFileListItemWidget.h"
#include "PathDroneMatchItemWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Engine/GameInstance.h"
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
	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnBackClicked);
	}
	PopulateModeComboBox();

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
			DroneItem->SetAsDroneItem(Desc.DroneId, Desc.Name);
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
				Item->SetMatchedLabel(FString::Printf(TEXT("-> Drone %d"), *Matched));
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
	NetMgr->SendArrayTaskFromData(DispatchMap, DispatchMode, Callback);

	SetStatusMessage(FString::Printf(TEXT("正在派发... mode=%s"), *DroneCommandModeToProtocolString(DispatchMode)));
	if (DispatchButton) DispatchButton->SetIsEnabled(false);
}

void USequenceDispatchPanelWidget::OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	DispatchMode = DroneCommandModeFromProtocolString(SelectedItem);
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
