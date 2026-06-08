#include "DronePathPlaybackWidget.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "PathEditor/DronePlaybackManager.h"
#include "PathFileListItemWidget.h"

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
	if (PlaybackManager && IsValid(PlaybackManager))
	{
		PlaybackManager->StopPlayback();
		SetStatusMessage(TEXT("Playback stopped"));
		return;
	}

	SetStatusMessage(TEXT("No active playback"));
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

	SetStatusMessage(FString::Printf(TEXT("Selected %s"), *FPaths::GetCleanFilename(SelectedFilePath)));
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
