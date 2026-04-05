// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOps/Drone/DroneCommandSenderComponent.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

// Wire format: 32 bytes, little-endian
#pragma pack(push, 1)
struct FMultiDroneUDPPacket
{
	double  Timestamp;  // 8
	float   X;          // 4  NED north [m]
	float   Y;          // 4  NED east  [m]
	float   Z;          // 4  NED down  [m]
	int32   Mode;       // 4
	int32   DroneMask;  // 4
	int32   Sequence;   // 4
	// total = 32
};
#pragma pack(pop)

UDroneCommandSenderComponent::UDroneCommandSenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDroneCommandSenderComponent::BeginPlay()
{
	Super::BeginPlay();
	InitSocket();
}

void UDroneCommandSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseSocket();
	Super::EndPlay(EndPlayReason);
}

bool UDroneCommandSenderComponent::InitSocket()
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DroneCommandSender: No socket subsystem"));
		return false;
	}

	SenderSocket = SocketSub->CreateSocket(NAME_DGram, TEXT("DroneCommandSender"), false);
	if (!SenderSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("DroneCommandSender: Failed to create UDP socket"));
		return false;
	}

	RemoteAddr = SocketSub->CreateInternetAddr();
	bool bIsValid = false;
	RemoteAddr->SetIp(*UnifiedControllerIP, bIsValid);
	RemoteAddr->SetPort(UnifiedControllerPort);

	if (!bIsValid)
	{
		UE_LOG(LogTemp, Error, TEXT("DroneCommandSender: Invalid IP address: %s"), *UnifiedControllerIP);
		CloseSocket();
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("DroneCommandSender: Socket ready -> %s:%d"), *UnifiedControllerIP, UnifiedControllerPort);
	return true;
}

void UDroneCommandSenderComponent::CloseSocket()
{
	if (SenderSocket)
	{
		ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSub)
		{
			SocketSub->DestroySocket(SenderSocket);
		}
		SenderSocket = nullptr;
	}
}

bool UDroneCommandSenderComponent::SendPacket(const FMultiDroneControlPacket& Packet)
{
	if (!SenderSocket || !RemoteAddr.IsValid())
	{
		return false;
	}

	// Throttle
	const double Now = FPlatformTime::Seconds();
	if (MinSendIntervalSec > 0.0f && (Now - LastSendTime) < MinSendIntervalSec)
	{
		return false;
	}
	LastSendTime = Now;

	FMultiDroneUDPPacket Wire;
	Wire.Timestamp = Packet.Timestamp;
	Wire.X         = Packet.X;
	Wire.Y         = Packet.Y;
	Wire.Z         = Packet.Z;
	Wire.Mode      = Packet.Mode;
	Wire.DroneMask = Packet.DroneMask;
	Wire.Sequence  = Packet.Sequence;

	static_assert(sizeof(FMultiDroneUDPPacket) == 32, "Packet size must be 32 bytes");

	int32 BytesSent = 0;
	const bool bOk = SenderSocket->SendTo(
		reinterpret_cast<const uint8*>(&Wire),
		sizeof(Wire),
		BytesSent,
		*RemoteAddr
	);

	if (!bOk || BytesSent != sizeof(Wire))
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneCommandSender: SendTo failed (sent %d / %d bytes)"), BytesSent, (int32)sizeof(Wire));
		return false;
	}

	LastPacket = Packet;
	return true;
}

void UDroneCommandSenderComponent::SendSingleDroneCommand(int32 DroneId, FVector NedTarget, int32 Mode)
{
	if (DroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneCommandSender: Invalid DroneId %d"), DroneId);
		return;
	}

	// Look up BitIndex from registry
	UDroneRegistrySubsystem* Registry = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
		}
	}

	// 与 multi_ue_controller.py 当前选择规则保持一致：bit = (drone_id - 1)
	// 说明：Python 端目前按 drone_id 推导位掩码，而不是读取 UE 的 BitIndex。
	// 为避免“点A动B”，这里优先使用 DroneId 推导掩码；若 Registry 中配置不一致则打印告警。
	const int32 MaskBitFromDroneId = DroneId - 1;
	if (MaskBitFromDroneId < 0 || MaskBitFromDroneId >= 31)
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneCommandSender: Invalid DroneId for mask mapping: %d"), DroneId);
		return;
	}

	if (Registry)
	{
		const int32 RegistryBitIndex = Registry->GetDroneBitIndex(DroneId);
		if (RegistryBitIndex >= 0 && RegistryBitIndex != MaskBitFromDroneId)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("DroneCommandSender: BitIndex mismatch for DroneId %d (Registry=%d, Using=%d). Consider aligning config."),
				DroneId, RegistryBitIndex, MaskBitFromDroneId);
		}
	}

	const int32 Mask = (1 << MaskBitFromDroneId);
	SendMultiDroneCommand(Mask, NedTarget, Mode);
}

void UDroneCommandSenderComponent::SendMultiDroneCommand(int32 DroneMask, FVector NedTarget, int32 Mode)
{
	if (!SenderSocket && !InitSocket())
	{
		return;
	}

	FMultiDroneControlPacket Packet;
	Packet.Timestamp  = FPlatformTime::Seconds();
	// 兼容当前 multi_ue_controller.py 协议：接收端会将 X/Y/Z 按 UE厘米 再转换到NED米。
	// 因此这里把 NED(m) 反变换为 UE(cm) 写入包体，避免在接收端被二次缩放导致几乎不动。
	Packet.X          = NedTarget.X * 100.0f;
	Packet.Y          = NedTarget.Y * 100.0f;
	Packet.Z          = -NedTarget.Z * 100.0f;
	Packet.Mode       = Mode;
	Packet.DroneMask  = DroneMask;
	Packet.Sequence   = ++Sequence;

	if (SendPacket(Packet))
	{
		UE_LOG(LogTemp, Verbose, TEXT("DroneCommandSender: Sent mask=0x%X NED=(%.2f,%.2f,%.2f) Mode=%d Seq=%d"),
			DroneMask, NedTarget.X, NedTarget.Y, NedTarget.Z, Mode, Sequence);
	}
}

void UDroneCommandSenderComponent::SendHoverCommand(int32 DroneId)
{
	// Look up current NED position from registry, then send Mode=0
	UDroneRegistrySubsystem* Registry = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
		}
	}

	FVector HoverNed = FVector::ZeroVector;
	if (Registry)
	{
		FDroneTelemetrySnapshot Snap;
		if (Registry->GetTelemetry(DroneId, Snap))
		{
			HoverNed = Snap.NedLocation;
		}
	}

	SendSingleDroneCommand(DroneId, HoverNed, 0);
}
