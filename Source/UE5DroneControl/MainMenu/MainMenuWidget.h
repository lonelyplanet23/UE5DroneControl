// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Network/DroneHttpClient.h"
#include "MainMenuWidget.generated.h"

class AMainMenuPlayerController;

/**
 * 主菜单 Widget 基类（C++ 骨架）
 *
 * UI 程序员需要在蓝图子类 WBP_MainMenu 中实现以下区域：
 *
 * [区域 1] 无人机注册模块
 *   - 无人机 ID 输入框 + IP 地址输入框 + "注册"按钮
 *   - 已注册无人机列表（可删除）
 *   - 调用 RegisterDrone() / UnregisterDrone()
 *
 * [区域 2] 通用设置模块
 *   - 后端 WebSocket 地址输入框
 *   - 地图中心坐标（经纬度）输入框
 *   - 坐标服务选择（Simple / Cesium）
 *   - 调用 SaveSettings() / LoadSettings()
 *
 * [区域 3] 导航按钮
 *   - "队列编辑" 按钮 → 调用 OnGoToQueueEditorClicked()
 *   - "预演" 按钮     → 调用 OnGoToPreviewClicked()
 */
UCLASS(Abstract)
class UE5DRONECONTROL_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:


	UPROPERTY() FString BackendAddress;
	UPROPERTY() float MapCenterLat;
	UPROPERTY() float MapCenterLon;
	UPROPERTY() bool bUseCesium;
	
	// -----------------------------------------------------------------------
	// 生命周期
	// -----------------------------------------------------------------------
	virtual void NativeConstruct() override;

	// -----------------------------------------------------------------------
	// 无人机注册（供蓝图按钮绑定）
	// -----------------------------------------------------------------------

	/**
	 * 注册一架无人机到 DroneRegistrySubsystem
	 * @param DroneId   无人机 ID（1-based）
	 * @param IpAddress 无人机 IP 地址（当前仅记录，FDroneDescriptor 如需持久化 IP 请扩展该结构体）
	 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|DroneRegistry")
	void RegisterDrone(int32 DroneId, const FString& IpAddress);

	/**
	 * 从 DroneRegistrySubsystem 注销一架无人机
	 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|DroneRegistry")
	void UnregisterDrone(int32 DroneId);

	/**
	 * 获取当前已注册的所有无人机描述符（供列表刷新使用）
	 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|DroneRegistry")
	TArray<FString> GetRegisteredDronesSummary() const;

	// -----------------------------------------------------------------------
	// 通用设置（供蓝图输入框绑定）
	// -----------------------------------------------------------------------

	/**
	 * 保存通用设置到 GameInstance（跨关卡持久化）
	 * @param BackendAddress  后端 WebSocket 地址，如 "ws://192.168.1.100:8080"
	 * @param MapCenterLat    地图中心纬度
	 * @param MapCenterLon    地图中心经度
	 * @param bUseCesium      是否使用 Cesium 坐标服务
	 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|Settings")
	void SaveSettings(const FString& InBackendAddress, float InMapCenterLat, float InMapCenterLon, float InMapCenterAlt, bool InbUseCesium);

	/**
	 * 从 GameInstance 读取已保存的设置，填充到输出参数
	 * UI 程序员在 NativeConstruct / WBP 的 Construct 事件中调用此函数来初始化输入框
	 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|Settings")
	void LoadSettings(FString& OutBackendAddress, float& OutMapCenterLat, float& OutMapCenterLon, float& OutMapCenterAlt, bool& OutbUseCesium) const;

	// -----------------------------------------------------------------------
	// 导航（供蓝图按钮 OnClicked 绑定）
	// -----------------------------------------------------------------------

	/** "队列编辑"按钮点击 → 跳转到队列编辑关卡 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|Navigation")
	void OnGoToQueueEditorClicked();

	/** "预演"按钮点击 → 跳转到预演关卡 */
	UFUNCTION(BlueprintCallable, Category = "MainMenu|Navigation")
	void OnGoToPreviewClicked();

	/** 查询后端 WebSocket 是否已连接（供蓝图控制按钮可用状态） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MainMenu|Navigation")
	bool IsBackendConnected() const;

protected:
	// -----------------------------------------------------------------------
	// 蓝图可重写的事件（UI 程序员可在 WBP 中 override 以刷新列表等）
	// -----------------------------------------------------------------------

	/** 无人机注册成功后触发，UI 程序员可在此刷新列表显示 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MainMenu|DroneRegistry")
	void OnDroneRegistered(int32 DroneId);

	/** 无人机注销后触发 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MainMenu|DroneRegistry")
	void OnDroneUnregistered(int32 DroneId);

	/** 设置保存成功后触发（可用于显示 Toast 提示） */
	UFUNCTION(BlueprintImplementableEvent, Category = "MainMenu|Settings")
	void OnSettingsSaved();

	/** 尝试进入预演关卡但后端未连接时触发，UI 可在此显示警告弹窗 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MainMenu|Navigation")
	void OnGoToPreviewBlockedByNoConnection();

private:
	UPROPERTY()
	int32 PendingBackendRegisterDroneId = 0;

	UPROPERTY()
	FString PendingBackendRegisterIpAddress;

	UFUNCTION()
	void HandleBackendRegisterResponse(bool bSuccess, const FString& ResponseBody);

	/** 获取 PlayerController（类型安全） */
	AMainMenuPlayerController* GetMainMenuPC() const;
};
