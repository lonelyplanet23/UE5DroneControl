#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DronePathPlaybackWidget.generated.h"

class ADronePlaybackManager;
class UButton;
class UScrollBox;
class UTextBlock;
class UPathFileListItemWidget;

UCLASS()
class UE5DRONECONTROL_API UDronePathPlaybackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> FileListScrollBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> StopButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedFileText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path Playback")
	TSubclassOf<UPathFileListItemWidget> FileListItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path Playback")
	TSubclassOf<ADronePlaybackManager> PlaybackManagerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path Playback")
	TSubclassOf<AActor> PlaybackDroneActorClass;

	UFUNCTION(BlueprintCallable, Category = "Drone Path Playback")
	void RefreshFileList();

	UFUNCTION(BlueprintCallable, Category = "Drone Path Playback")
	void PlaySelectedFile();

	UFUNCTION(BlueprintCallable, Category = "Drone Path Playback")
	void StopCurrentPlayback();

protected:
	virtual void NativeConstruct() override;

private:
	UPROPERTY()
	TObjectPtr<ADronePlaybackManager> PlaybackManager;

	UPROPERTY()
	TMap<FString, TObjectPtr<UPathFileListItemWidget>> FileItemsByPath;

	FString SelectedFilePath;

	void SelectFile(const FString& FilePath);
	ADronePlaybackManager* GetOrCreatePlaybackManager();
	void SetStatusMessage(const FString& Message);
};
