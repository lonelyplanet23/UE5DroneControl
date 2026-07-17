#include "DronePathPlaybackWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "PathEditor/DronePathActor.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "PathEditor/DronePlaybackManager.h"
#include "PathFileListItemWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UDronePathPlaybackWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RefreshButton)
	{
		RefreshButton->OnClicked.AddDynamic(this, &UDronePathPlaybackWidget::RefreshFileList);
	}
	if (PlayButton)
	{
		PlayButton->OnClicked.AddDynamic(this, &UDronePathPlaybackWidget::PlaySelectedFile);
	}
	if (StopButton)
	{
		StopButton->OnClicked.AddDynamic(this, &UDronePathPlaybackWidget::StopCurrentPlayback);
	}
	if (LoopCheckBox)
	{
		LoopCheckBox->OnCheckStateChanged.AddDynamic(this, &UDronePathPlaybackWidget::OnLoopCheckChanged);
		LoopCheckBox->SetIsEnabled(false); // 未选文件时不可用
	}

	RefreshFileList();
}

void UDronePathPlaybackWidget::RefreshFileList()
{
	if (!FileListScrollBox)
	{
		SetStatusMessage(TEXT("FileListScrollBox not bound"));
		return;
	}

	FileListScrollBox->ClearChildren();
	FileItemsByPath.Empty();
	SelectedFilePath.Empty();

	if (SelectedFileText)
	{
		SelectedFileText->SetText(FText::FromString(TEXT("No file selected")));
	}

	const FString SearchDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DronePaths"));
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *FPaths::Combine(SearchDir, TEXT("*.json")), true, false);
	FoundFiles.Sort();

	if (FoundFiles.IsEmpty())
	{
		SetStatusMessage(TEXT("No JSON files found in Saved/DronePaths"));
		return;
	}

	for (const FString& FileName : FoundFiles)
	{
		if (!FileListItemClass)
		{
			SetStatusMessage(TEXT("FileListItemClass not set"));
			return;
		}

		UPathFileListItemWidget* Item = CreateWidget<UPathFileListItemWidget>(this, FileListItemClass);
		if (!Item)
		{
			continue;
		}

		const FString FullPath = FPaths::Combine(SearchDir, FileName);
		Item->SetFileInfo(FileName, FullPath);
		Item->OnClicked.BindUObject(this, &UDronePathPlaybackWidget::SelectFile);
		FileItemsByPath.Add(FullPath, Item);

		USizeBox* SizeBox = NewObject<USizeBox>(FileListScrollBox);
		SizeBox->SetMinDesiredHeight(40.0f);
		SizeBox->AddChild(Item);
		FileListScrollBox->AddChild(SizeBox);
	}

	SetStatusMessage(FString::Printf(TEXT("Found %d JSON files"), FoundFiles.Num()));
}

void UDronePathPlaybackWidget::PlaySelectedFile()
{
	if (SelectedFilePath.IsEmpty())
	{
		SetStatusMessage(TEXT("Select a path file first"));
		return;
	}

	FDronePathsSaveData LoadedData;
	if (!UDronePathSaveLibrary::LoadPathsFromJson(SelectedFilePath, LoadedData))
	{
		SetStatusMessage(TEXT("Failed to load selected JSON"));
		return;
	}

	if (LoadedData.Paths.IsEmpty())
	{
		SetStatusMessage(TEXT("Selected JSON contains no paths"));
		return;
	}

	ADronePlaybackManager* Manager = GetOrCreatePlaybackManager();
	if (!Manager)
	{
		SetStatusMessage(TEXT("Failed to create playback manager"));
		return;
	}

	Manager->bUseExistingShadowDrones = true;
	Manager->DroneActorClass = PlaybackDroneActorClass;
	Manager->PlayFromData(LoadedData);

	SetStatusMessage(FString::Printf(TEXT("Playing %s"), *FPaths::GetCleanFilename(SelectedFilePath)));
}

void UDronePathPlaybackWidget::StopCurrentPlayback()
{
	// 统一走全局停止入口：停掉所有 manager 并销毁所有路径 Actor，
	// 覆盖"循环路径永不 Completed"与"播放/停止分属不同实例"的情况。
	ADronePlaybackManager::StopAndClearAllInWorld(GetWorld());
	SetStatusMessage(TEXT("Playback stopped"));
}

void UDronePathPlaybackWidget::SelectFile(const FString& FilePath)
{
	SelectedFilePath = FilePath;

	for (const TPair<FString, TObjectPtr<UPathFileListItemWidget>>& Pair : FileItemsByPath)
	{
		if (Pair.Value)
		{
			Pair.Value->SetSelected(Pair.Key == SelectedFilePath);
		}
	}

	if (SelectedFileText)
	{
		SelectedFileText->SetText(FText::FromString(FPaths::GetCleanFilename(SelectedFilePath)));
	}

	// 同步循环勾选框到该文件当前的循环状态。
	UpdateLoopCheckBoxFromSelectedFile();

	SetStatusMessage(FString::Printf(TEXT("Selected %s"), *FPaths::GetCleanFilename(SelectedFilePath)));
}

void UDronePathPlaybackWidget::UpdateLoopCheckBoxFromSelectedFile()
{
	if (!LoopCheckBox)
	{
		return;
	}

	if (SelectedFilePath.IsEmpty())
	{
		LoopCheckBox->SetIsEnabled(false);
		return;
	}

	FDronePathsSaveData LoadedData;
	bool bAnyLoop = false;
	if (UDronePathSaveLibrary::LoadPathsFromJson(SelectedFilePath, LoadedData))
	{
		// 该文件里只要有一条路径开了循环，就显示为勾选。
		for (const FDronePathSaveData& Path : LoadedData.Paths)
		{
			if (Path.bClosedLoop)
			{
				bAnyLoop = true;
				break;
			}
		}
	}

	LoopCheckBox->SetIsEnabled(true);
	// 直接设状态，不触发写回：SetIsChecked 不会广播 OnCheckStateChanged。
	LoopCheckBox->SetIsChecked(bAnyLoop);
}

void UDronePathPlaybackWidget::OnLoopCheckChanged(bool bIsChecked)
{
	if (SelectedFilePath.IsEmpty())
	{
		SetStatusMessage(TEXT("请先选择一个路径文件"));
		return;
	}

	FDronePathsSaveData LoadedData;
	if (!UDronePathSaveLibrary::LoadPathsFromJson(SelectedFilePath, LoadedData))
	{
		SetStatusMessage(TEXT("读取文件失败，无法修改循环状态"));
		return;
	}

	// 把该文件里所有路径的循环状态统一设为勾选值，并写回同一个文件。
	for (FDronePathSaveData& Path : LoadedData.Paths)
	{
		Path.bClosedLoop = bIsChecked;
	}

	if (UDronePathSaveLibrary::SavePathsDataToFile(SelectedFilePath, LoadedData))
	{
		SetStatusMessage(bIsChecked
			? FString::Printf(TEXT("已设为循环播放：%s"), *FPaths::GetCleanFilename(SelectedFilePath))
			: FString::Printf(TEXT("已取消循环：%s"), *FPaths::GetCleanFilename(SelectedFilePath)));
	}
	else
	{
		SetStatusMessage(TEXT("写回文件失败"));
	}
}

ADronePlaybackManager* UDronePathPlaybackWidget::GetOrCreatePlaybackManager()
{
	if (PlaybackManager && IsValid(PlaybackManager))
	{
		return PlaybackManager;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<ADronePlaybackManager> ManagerClass = PlaybackManagerClass;
	if (!ManagerClass)
	{
		ManagerClass = ADronePlaybackManager::StaticClass();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PlaybackManager = World->SpawnActor<ADronePlaybackManager>(ManagerClass, FTransform::Identity, SpawnParams);
	return PlaybackManager;
}

void UDronePathPlaybackWidget::SetStatusMessage(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}

	UE_LOG(LogTemp, Log, TEXT("[DronePathPlaybackWidget] %s"), *Message);
}
