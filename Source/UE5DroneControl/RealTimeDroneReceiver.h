#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h" // 继承你的主角基类
#include "Networking.h"
#include "Sockets.h"
#include "RealTimeDroneReceiver.generated.h"

/**
 * 真实飞机接收端 (ARealTimeDroneReceiver)
 * 采用主线程轮询 (Polling) 模式，解决多线程委托在某些环境下不触发的问题。
 */
UCLASS()
class UE5DRONECONTROL_API ARealTimeDroneReceiver : public AUE5DroneControlCharacter
{
	GENERATED_BODY()

public:
	// 构造函数
	ARealTimeDroneReceiver();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// 重写 Tick 每一帧主动检查数据
	virtual void Tick(float DeltaTime) override;

	// --- 参数配置区 ---

	// 监听端口
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	int32 ListenPort = 8888;

	// 平滑移动速度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float SmoothSpeed = 5.0f;

	// 坐标缩放 (1.0 = 不缩放)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float ScaleFactor = 1.0f;

	// 自动转向
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bAutoFaceTarget = true;

private:
	// 网络 Socket
	FSocket* ListenSocket;

	// 目标位置缓存
	FVector TargetLocation;

	// 处理数据包函数
	void ProcessPacket(const TArray<uint8>& Data);
};