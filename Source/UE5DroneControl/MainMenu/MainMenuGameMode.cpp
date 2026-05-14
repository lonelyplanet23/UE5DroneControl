// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuGameMode.h"
#include "MainMenuPlayerController.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	// 主菜单不需要 Pawn，使用专用 PlayerController
	PlayerControllerClass = AMainMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}
