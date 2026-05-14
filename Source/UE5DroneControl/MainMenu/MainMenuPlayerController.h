// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MainMenuPlayerController.generated.h"

class UMainMenuWidget;

/**
 * 主菜单关卡的 PlayerController
 *
 * 职责：
 * - 显示主菜单 Widget（UMainMenuWidget）
 * - 提供跳转到队列编辑关卡（RuntimeInteraction）的接口
 * - 提供跳转到预演关卡（DroneOps）的接口
 * - 保存/读取通用设置（通过 GameInstance 或 SaveGame）
 */
UCLASS()
class UE5DRONECONTROL_API AMainMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMainMenuPlayerController();

protected:
	virtual void BeginPlay() override;

public:
	// -----------------------------------------------------------------------
	// 关卡跳转
	// -----------------------------------------------------------------------

	/** 跳转到队列编辑关卡（RuntimeInteraction） */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|Navigation")
	void GoToQueueEditor();

	/** 跳转到预演关卡（DroneOps） */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|Navigation")
	void GoToPreview();

	// -----------------------------------------------------------------------
	// Widget 类引用（在蓝图子类中赋值）
	// -----------------------------------------------------------------------

	/** 主菜单 Widget 类，由蓝图子类 BP_MainMenuPlayerController 赋值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MainMenu|UI")
	TSubclassOf<UUserWidget> MainMenuWidgetClass;

	// -----------------------------------------------------------------------
	// 关卡名称配置（可在蓝图或 DefaultGame.ini 中覆盖）
	// -----------------------------------------------------------------------

	/** 队列编辑关卡名称，默认 "EditMap" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MainMenu|Navigation")
	FName QueueEditorLevelName = FName("EditMap");

	/** 预演关卡名称，默认 "DroneOpsMap" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MainMenu|Navigation")
	FName PreviewLevelName = FName("DroneOpsMap");

private:
	/** 当前显示的主菜单 Widget 实例 */
	UPROPERTY()
	TObjectPtr<UUserWidget> MainMenuWidgetInstance;

	/** 创建并显示主菜单 Widget */
	void ShowMainMenuWidget();
};
