#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LocalPreviewIsolationToggleWidget.generated.h"

class UCheckBox;
class UTextBlock;
class UBorder;
class UDroneNetworkManager;

/**
 * 纯本地预演（隔离后端）Toggle 控件。
 * 完全由 C++ 构建 UI，不需要蓝图。
 * 视觉优先级高、始终可见，不被普通面板遮挡。
 */
UCLASS()
class UE5DRONECONTROL_API ULocalPreviewIsolationToggleWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeOnInitialized() override;

private:
	/** 构建运行时 Widget 树（CheckBox + 标签 + 状态横幅）。 */
	void BuildWidgetTree();

	/** CheckBox 状态变化回调。 */
	UFUNCTION()
	void OnToggleChanged(bool bIsChecked);

	/** DroneNetworkManager 隔离状态变化回调（外部触发，如关卡退出复位）。 */
	UFUNCTION()
	void OnIsolationStateChanged(bool bIsolated);

	UPROPERTY()
	TObjectPtr<UCheckBox> IsolationCheckBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY()
	TObjectPtr<UBorder> StatusBanner;

	UPROPERTY()
	TObjectPtr<UTextBlock> BannerText;

	UPROPERTY()
	TObjectPtr<UDroneNetworkManager> CachedNetworkManager;

	/** 避免 CheckBox 回调与外部状态变化回调互相触发。 */
	bool bSuppressCallback = false;
};
