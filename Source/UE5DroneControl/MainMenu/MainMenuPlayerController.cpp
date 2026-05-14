// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"

AMainMenuPlayerController::AMainMenuPlayerController()
{
	// 主菜单始终显示鼠标光标
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 设置输入模式为纯 UI（主菜单不需要游戏输入）
	FInputModeUIOnly InputMode;
	SetInputMode(InputMode);

	ShowMainMenuWidget();
}

void AMainMenuPlayerController::ShowMainMenuWidget()
{
	if (!MainMenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("AMainMenuPlayerController: MainMenuWidgetClass is not set. "
			"Please assign it in BP_MainMenuPlayerController."));
		return;
	}

	MainMenuWidgetInstance = CreateWidget<UUserWidget>(this, MainMenuWidgetClass);
	if (MainMenuWidgetInstance)
	{
		MainMenuWidgetInstance->AddToViewport(0);
	}
}

void AMainMenuPlayerController::GoToQueueEditor()
{
	UGameplayStatics::OpenLevel(this, QueueEditorLevelName);
}

void AMainMenuPlayerController::GoToPreview()
{
	UGameplayStatics::OpenLevel(this, PreviewLevelName);
}
