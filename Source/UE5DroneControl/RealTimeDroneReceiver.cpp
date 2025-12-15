#include "RealTimeDroneReceiver.h"

// --- 引入必要的底层头文件 ---
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Common/UdpSocketBuilder.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/CapsuleComponent.h" 

ARealTimeDroneReceiver::ARealTimeDroneReceiver()
{
	PrimaryActorTick.bCanEverTick = true;
	TargetLocation = FVector::ZeroVector;
	ListenSocket = nullptr;

	// === 【修改】智能碰撞设置 ===
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// 1. 确保碰撞是开启的 (Profile = Pawn)，这样它才能检测到地板
		Capsule->SetCollisionProfileName(TEXT("Pawn"));

		// 2. 但是！我们要忽略其他“Pawn”（也就是玩家和其他飞机）
		// 这样你们两个飞机重叠时才不会互相挤飞
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

		// 顺便忽略一下摄像机，防止遮挡视线
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
}

void ARealTimeDroneReceiver::BeginPlay()
{
	Super::BeginPlay();

	TargetLocation = GetActorLocation();

	// === 1. 绑定到 0.0.0.0 (监听所有网卡) ===
	FIPv4Endpoint Endpoint(FIPv4Address::Any, ListenPort);

	ListenSocket = FUdpSocketBuilder(TEXT("RealTimePollingSocket"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(2 * 1024 * 1024);

	if (ListenSocket)
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [RealTimeDrone] 监听启动 (Polling)! Port: %d <<<"), ListenPort);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT(">>> [RealTimeDrone] 错误: 端口 %d 绑定失败! <<<"), ListenPort);
	}
}

void ARealTimeDroneReceiver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (ListenSocket)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}
}

void ARealTimeDroneReceiver::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// === 2. 主动轮询数据 ===
	if (ListenSocket)
	{
		uint32 Size;
		while (ListenSocket->HasPendingData(Size))
		{
			TArray<uint8> ReceivedData;
			ReceivedData.SetNumUninitialized(FMath::Min(Size, 65507u));

			int32 Read = 0;
			TSharedRef<FInternetAddr> SenderAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

			if (ListenSocket->RecvFrom(ReceivedData.GetData(), ReceivedData.Num(), Read, *SenderAddr))
			{
				if (Read > 0)
				{
					ProcessPacket(ReceivedData);
				}
			}
		}
	}

	// === 3. 平滑移动 ===
	FVector CurrentLoc = GetActorLocation();
	FVector NewLoc = FMath::VInterpTo(CurrentLoc, TargetLocation, DeltaTime, SmoothSpeed);

	// 【关键修改】改回 true (Sweep/扫掠)
	// 启用 Sweep 后，飞机移动时会进行碰撞检测。
	// 当 Python 发送 Z=0 时，飞机向下飞，碰到地板就会停住，从而“站在”地上，而不是穿过去。
	SetActorLocation(NewLoc, true);

	// === 4. 自动转向 ===
	if (bAutoFaceTarget)
	{
		FVector Direction = (NewLoc - CurrentLoc);
		if (Direction.SizeSquared() > 1.0f)
		{
			FRotator TargetRot = UKismetMathLibrary::MakeRotFromX(Direction);
			FRotator NewRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 10.0f);
			SetActorRotation(FRotator(0, NewRot.Yaw, 0));
		}
	}
}

void ARealTimeDroneReceiver::ProcessPacket(const TArray<uint8>& Data)
{
	if (Data.Num() == sizeof(FDroneSocketData))
	{
		FDroneSocketData Packet;
		FMemory::Memcpy(&Packet, Data.GetData(), sizeof(FDroneSocketData));

		FVector NewTarget(Packet.X, Packet.Y, Packet.Z);
		NewTarget *= ScaleFactor;

		// 去重日志
		if (!NewTarget.Equals(TargetLocation, 1.0f))
		{
			UE_LOG(LogTemp, Log, TEXT(">>> [New Data] 收到新坐标: %s | Mode: %d"), *NewTarget.ToString(), Packet.Mode);
		}

		TargetLocation = NewTarget;

		// 屏幕调试
		if (GEngine)
		{
			FString DebugMsg = FString::Printf(TEXT("Recv: %s (Mode: %d)"), *NewTarget.ToString(), Packet.Mode);
			GEngine->AddOnScreenDebugMessage(123, 0.1f, FColor::Green, DebugMsg);
		}
	}
}