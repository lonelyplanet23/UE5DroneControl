// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PreviewConfirmPopupWidget.generated.h"

class UButton;
class UTextBlock;

/** 关闭路径编辑 Toggle 后弹出的三选确认结果 */
UENUM(BlueprintType)
enum class EPreviewConfirmChoice : uint8
{
	LocalPreview,	// 本地预演：仅驱动影子机，不调用后端
	Dispatch,		// 指令派发：发送 /api/arrays
	Cancel			// 取消：销毁临时路径与缓存
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPreviewConfirmChosen, EPreviewConfirmChoice, Choice);

/**
 * 路径编辑结束时的三选弹窗 C++ 基类：本地预演 / 指令派发 / 取消。
 * 蓝图子类（WBP_PreviewConfirmPopup）绑定三个同名按钮即可。
 */
UCLASS()
class UE5DRONECONTROL_API UPreviewConfirmPopupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用户做出选择后广播；广播后弹窗自动关闭 */
	UPROPERTY(BlueprintAssignable, Category = "PreviewConfirm")
	FOnPreviewConfirmChosen OnChoiceMade;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* LocalPreviewButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* DispatchButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CancelButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* MessageText = nullptr;

	/** 设置提示文字（可选） */
	UFUNCTION(BlueprintCallable, Category = "PreviewConfirm")
	void SetMessage(const FString& Message);

	// ===== 攻击确认模式 =====

	/** 设置为攻击确认模式 */
	UFUNCTION(BlueprintCallable, Category = "AttackConfirm")
	void SetAttackConfirmData(int32 InDroneId, int32 InTargetId, const FVector& InTargetLocation);

	/** 攻击确认广播 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAttackConfirmMade,
		int32, DroneId, int32, TargetId, bool, bAttack);
	UPROPERTY(BlueprintAssignable, Category = "AttackConfirm")
	FOnAttackConfirmMade OnAttackConfirmMade;

	/** 攻击按钮（复用 LocalPreviewButton） */
	UFUNCTION(BlueprintCallable, Category = "AttackConfirm")
	void SetAttackButtonText(const FString& Text);

	/** 不攻击按钮（复用 CancelButton） */
	UFUNCTION(BlueprintCallable, Category = "AttackConfirm")
	void SetDeclineButtonText(const FString& Text);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleLocalPreviewClicked();

	UFUNCTION()
	void HandleDispatchClicked();

	UFUNCTION()
	void HandleCancelClicked();

	void MakeChoice(EPreviewConfirmChoice Choice);

	// 攻击确认数据
	int32 AttackDroneId = 0;
	int32 AttackTargetId = 0;
	FVector AttackTargetLocation = FVector::ZeroVector;
	bool bIsAttackConfirmMode = false;

	void HandleAttackConfirmClicked();
	void HandleAttackDeclineClicked();
};
