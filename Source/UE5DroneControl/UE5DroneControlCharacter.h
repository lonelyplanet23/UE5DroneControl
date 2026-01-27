// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "UE5DroneControlCharacter.generated.h"

// 【改回经典写法】使用前置声明
class UCameraComponent;
class USpringArmComponent;
class FSocket; // 声明 Socket 类

// --- 【新增】定义通信协议数据包 ---
// #pragma pack(push, 1) 保证字节对齐，防止 C++ 和 Python 解析不一致
#pragma pack(push, 1)
struct FDroneSocketData
{
    double Timestamp; // 8 bytes
    float X;          // 4 bytes
    float Y;          // 4 bytes
    float Z;          // 4 bytes
    int32 Mode;       // 4 bytes (0=待机, 1=飞行)
};
#pragma pack(pop)

/**
 * A controllable top-down perspective character
 */
UCLASS(abstract)
class AUE5DroneControlCharacter : public ACharacter
{
    GENERATED_BODY()

private:
    /** Top down camera */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    UCameraComponent* TopDownCameraComponent; // 【改回原始指针 *】

    /** Camera boom positioning the camera above the character */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    USpringArmComponent* CameraBoom; // 【改回原始指针 *】

public:
    /** Constructor */
    AUE5DroneControlCharacter();

    /** Initialization */
    virtual void BeginPlay() override;

    // 销毁时记得关闭 Socket
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Update - 我们的平滑逻辑就在每一帧运行 */
    virtual void Tick(float DeltaSeconds) override;

    // 绑定输入函数
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    /** Returns the camera component **/
    UCameraComponent* GetTopDownCameraComponent() const { return TopDownCameraComponent; }

    /** Returns the Camera Boom component **/
    USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

    // 【修正】只保留这个主函数即可
    UFUNCTION(BlueprintCallable, Category = "Drone Flight")
    void Input_Lift(float Value);

    // --- 【新增】相机切换函数 ---
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void ToggleCameraView();

    // --- 【新增】视角切换函数（调用PlayerController的函数）---
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void SwitchToTopDownView();

    UFUNCTION(BlueprintCallable, Category = "Camera")
    void SwitchToRealTimeView();

    // --- 【新增】网络发送函数 (暴露给蓝图) ---
    // Mode: 0=悬停, 1=移动
    UFUNCTION(BlueprintCallable, Category = "Drone Network")
    void SendUDPData(FVector TargetLocation, int32 Mode);

protected:
    // --- 我们的核心变量 ---

    // 目标高度
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Flight")
    float TargetHeight;

    // 升降速度
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Flight")
    float LiftSpeed;

    // 平滑阻尼系数
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Flight")
    float InterpSpeed;

    // 高度限制
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Flight")
    float MinHeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Flight")
    float MaxHeight;

    // --- 【新增】相机状态变量 ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    bool bIsTopDownView;

    // --- 【新增】网络相关变量 ---
    FSocket* SenderSocket;
    TSharedPtr<class FInternetAddr> RemoteAddr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Network")
    FString RemoteIP = "127.0.0.1"; // 目标 IP (填 PC B 的 IP)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Network")
    int32 RemotePort = 8888; // 目标端口

    // 计时器：用于控制发送频率 (避免把网卡发爆)
    float SendTimer = 0.0f;
    float SendInterval = 0.1f; // 10Hz (每 0.1秒发一次)
};