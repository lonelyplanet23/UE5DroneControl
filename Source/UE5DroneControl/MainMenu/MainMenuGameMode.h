// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MainMenuGameMode.generated.h"

/**
 * 主菜单关卡的 GameMode
 * 纯 2D UI 关卡，无 3D Pawn，禁用默认 Pawn 生成
 */
UCLASS()
class UE5DRONECONTROL_API AMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMainMenuGameMode();
};
