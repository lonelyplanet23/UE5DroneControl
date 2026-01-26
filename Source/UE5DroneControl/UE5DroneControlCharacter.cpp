// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE5DroneControlCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"

// 【网络库头文件】
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Common/UdpSocketBuilder.h" // 【新增】使用 Builder 模式创建 UDP Socket，避开 LAN 报错

AUE5DroneControlCharacter::AUE5DroneControlCharacter()
{
    // Set size for player capsule
    GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

    // Don't rotate character to camera direction
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    // Configure character movement
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
    GetCharacterMovement()->bConstrainToPlane = true;
    GetCharacterMovement()->bSnapToPlaneAtStart = true;

    // Create the camera boom component
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->SetUsingAbsoluteRotation(true);
    CameraBoom->TargetArmLength = 3600.f; // 稍微拉远一点视角
    CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
    CameraBoom->bDoCollisionTest = false;

    // Create the camera component
    TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
    TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    TopDownCameraComponent->bUsePawnControlRotation = false;

    // Activate ticking in order to update the cursor every frame.
    // 【关键】必须开启 Tick
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // --- 初始化我们的变量 ---
    TargetHeight = 200.0f;
    LiftSpeed = 300.0f;
    InterpSpeed = 4.0f;
    MinHeight = 50.0f;
    MaxHeight = 6000.0f;

    // 初始化网络指针
    SenderSocket = nullptr;
}

void AUE5DroneControlCharacter::BeginPlay()
{
    Super::BeginPlay();

    // 游戏开始时，让 Mesh 直接对齐到目标高度
    if (USkeletalMeshComponent* MyMesh = GetMesh())
    {
        FVector CurrentRelLoc = MyMesh->GetRelativeLocation();
        MyMesh->SetRelativeLocation(FVector(CurrentRelLoc.X, CurrentRelLoc.Y, TargetHeight));
    }

    // --- 【修改】使用 Builder 创建 UDP Socket (更稳健) ---
    // 这样就不需要担心 LAN 宏定义的问题了
    SenderSocket = FUdpSocketBuilder(TEXT("DroneSenderSocket"))
        .AsReusable()
        .WithBroadcast(); // 允许广播，方便调试

    if (SenderSocket)
    {
        ISocketSubsystem* SocketSubs = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (SocketSubs)
        {
            // 2. 设置目标地址 (PC B 的 IP)
            // 注意：RemoteIP 是我们在编辑器里填的字符串 "192.168.x.x"
            RemoteAddr = SocketSubs->CreateInternetAddr();

            FIPv4Address IP;
            bool bIsValidIP = FIPv4Address::Parse(RemoteIP, IP); // 解析字符串IP

            if (bIsValidIP)
            {
                RemoteAddr->SetIp(IP.Value);
                RemoteAddr->SetPort(RemotePort);
                UE_LOG(LogTemp, Warning, TEXT("✅ UDP Socket Created! Sending to %s:%d"), *RemoteIP, RemotePort);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Invalid Remote IP: %s"), *RemoteIP);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Failed to create UDP Socket!"));
    }
}

void AUE5DroneControlCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    // 游戏结束时关闭 Socket，释放资源
    if (SenderSocket)
    {
        SenderSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SenderSocket);
        SenderSocket = nullptr;
    }
}

void AUE5DroneControlCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // 绑定 "Lift" 轴
    PlayerInputComponent->BindAxis("Lift", this, &AUE5DroneControlCharacter::Input_Lift);
}

void AUE5DroneControlCharacter::Input_Lift(float Value)
{
    if (Value != 0.0f)
    {
        float Delta = Value * LiftSpeed * GetWorld()->GetDeltaSeconds();
        TargetHeight += Delta;
        TargetHeight = FMath::Clamp(TargetHeight, MinHeight, MaxHeight);

        // 当我们在按 W/S 时，强制立即发送一次数据 (高频)
        // 这样手感更跟手
        FVector CurrentPos = GetMesh()->GetComponentLocation(); // 注意用 GetComponentLocation 拿真实的世界坐标
        SendUDPData(CurrentPos, 1); // Mode 1 = Moving
    }
}

// --- 【新增】发送函数 ---
void AUE5DroneControlCharacter::SendUDPData(FVector TargetLocation, int32 Mode)
{
    if (!SenderSocket || !RemoteAddr.IsValid()) return;

    // 1. 准备数据包
    FDroneSocketData Data;
    // 获取当前 UTC 时间戳
    Data.Timestamp = FDateTime::UtcNow().ToUnixTimestamp();

    // 如果我们是用 Mesh 偏移法，GetActorLocation() 只能拿到地面的 X,Y
    // 所以 X,Y 用 TargetLocation (可能是鼠标点，也可能是当前位置)
    // Z 我们希望发的是飞机的真实高度
    Data.X = TargetLocation.X;
    Data.Y = TargetLocation.Y;

    // 如果是 W/S 模式，TargetLocation.Z 已经包含了高度
    // 为了保险，我们可以再次读取 Mesh 的世界高度
    if (USkeletalMeshComponent* MyMesh = GetMesh())
    {
        Data.Z = MyMesh->GetComponentLocation().Z;
    }
    else
    {
        Data.Z = TargetLocation.Z;
    }

    Data.Mode = Mode;

    // 2. 发送
    int32 BytesSent = 0;
    SenderSocket->SendTo((uint8*)&Data, sizeof(FDroneSocketData), BytesSent, *RemoteAddr);
}

void AUE5DroneControlCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 平滑移动逻辑
    if (USkeletalMeshComponent* MyMesh = GetMesh())
    {
        FVector CurrentRelLoc = MyMesh->GetRelativeLocation();
        float NewZ = FMath::FInterpTo(CurrentRelLoc.Z, TargetHeight, DeltaSeconds, InterpSpeed);
        MyMesh->SetRelativeLocation(FVector(CurrentRelLoc.X, CurrentRelLoc.Y, NewZ));

        // 让摄像头支架 (CameraBoom) 也跟着升降
        if (CameraBoom)
        {
            FVector BoomLoc = CameraBoom->GetRelativeLocation();
            CameraBoom->SetRelativeLocation(FVector(BoomLoc.X, BoomLoc.Y, NewZ));
        }

        // --- 【新增】自动发送逻辑 (心跳包) ---
        // 即使不动，也每隔 0.1s 发送一次当前位置，保持同步
        SendTimer += DeltaSeconds;
        if (SendTimer >= SendInterval)
        {
            SendTimer = 0.0f;
            // Mode 0 = Idle (如果按了 W/S 或点击了移动，由那些事件去发高频包)
            SendUDPData(MyMesh->GetComponentLocation(), 0);
        }
    }
}