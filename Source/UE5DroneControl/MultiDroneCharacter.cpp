// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiDroneCharacter.h"
#include "RealTimeDroneReceiver.h"
#include "DroneOps/Drone/DroneSelectionComponent.h"
#include "DroneOps/Drone/DroneCommandSenderComponent.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOps/Network/DroneHttpClient.h"
#include "PathEditor/DronePathActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"

AMultiDroneCharacter::AMultiDroneCharacter()
{
	PrimaryActorTick.bCanEverTick = true;  // remote: Tick 需要  // remote: Tick 需要

	SelectionComponent = CreateDefaultSubobject<UDroneSelectionComponent>(TEXT("SelectionComponent"));
	CommandSenderComponent = CreateDefaultSubobject<UDroneCommandSenderComponent>(TEXT("CommandSenderComponent"));

	// 确保碰撞设置正确，支持鼠标悬停检测
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// 保持 Pawn 配置，但确保 Visibility 通道阻塞（鼠标检测需要）
		// 保持 Pawn 配置，但确保 Visibility 通道阻塞（鼠标检测需要）
		Capsule->SetCollisionProfileName(TEXT("Pawn"));
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		// 【关键】确保 Visibility 通道阻塞 - 鼠标悬停检测需要
		// 【关键】确保 Visibility 通道阻塞 - 鼠标悬停检测需要
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Capsule->SetSimulatePhysics(false);
	}
}

void AMultiDroneCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (SelectionComponent)
	{
		SelectionComponent->DroneId = DroneId;
		SelectionComponent->ThemeColor = ThemeColor;
	}

	if (SelectionWidgetComponent)
	{
		SelectionWidgetComponent->SetRelativeLocation(SelectionWidgetRelativeLocation);
		SelectionWidgetComponent->SetVisibility(false);

		UUserWidget* Widget = SelectionWidgetComponent->GetWidget();
		if (!Widget)
		{
			Widget = NewObject<UUserWidget>(this, UUserWidget::StaticClass());
			if (Widget)
			{
				UTextBlock* TextBlock = Widget->WidgetTree ? Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SelectedText")) : nullptr;
				if (TextBlock && Widget->WidgetTree)
				{
					TextBlock->SetText(SelectedWidgetText);
					TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow));
					Widget->WidgetTree->RootWidget = TextBlock;
				}
				SelectionWidgetComponent->SetWidget(Widget);
			}
		}
		else
		{
			if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("SelectedText"))))
			{
				TextBlock->SetText(SelectedWidgetText);
			}
		}
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	// remote: Registry 存为成员变量，供 Tick 使用
	// remote: Registry 存为成员变量，供 Tick 使用
	Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry)
	{
		return;
	}

	FDroneDescriptor Desc;
	Desc.Name            = DroneName;
	Desc.DroneId         = DroneId;
	Desc.MavlinkSystemId = MavlinkSystemId;
	Desc.BitIndex        = BitIndex;
	Desc.ThemeColor      = ThemeColor;
	Desc.UEReceivePort   = UEReceivePort;
	Desc.TopicPrefix     = TopicPrefix;

	Registry->RegisterDrone(Desc);
	Registry->RegisterSenderPawn(DroneId, this);

	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter: Registered %s (ID=%d, BitIndex=%d)"),
		*DroneName, DroneId, BitIndex);

	// remote: 订阅 assembly 事件
	// remote: 订阅 assembly 事件
	SubscribeToAssemblyEvents();

	// local: 订阅 power_on/reconnect 事件，用于上电时位置对齐
	if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
	{
		NetMgr->OnDroneWsEvent.AddUObject(this, &AMultiDroneCharacter::OnDroneWsEvent);

		// Catch up: if power_on arrived before this actor spawned, apply cached anchor now.
		double CachedLat, CachedLon, CachedAlt;
		if (NetMgr->GetCachedGpsAnchor(DroneId, CachedLat, CachedLon, CachedAlt))
		{
			UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter [%s]: Applying cached GPS anchor from before spawn"), *DroneName);
			OnDroneWsEvent(DroneId, TEXT("power_on"), CachedLat, CachedLon, CachedAlt);
		}
	}
}

void AMultiDroneCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
		{
			NetMgr->OnDroneWsEvent.RemoveAll(this);
		}
	}
}

void AMultiDroneCharacter::Tick(float DeltaTime)
{
	if (bIsPaused)
	{
		bSendClickTarget = false;
	}

	// Disable parent UDP send — shadow drones use WebSocket exclusively
	bEnableUDPSend = false;

	Super::Tick(DeltaTime);

	if (!Registry)
	{
		return;
	}

	FDroneTelemetrySnapshot MirrorSnap;
	if (Registry->GetTelemetry(DroneId, MirrorSnap))
	{
		MirrorDelayDistance = FVector::Dist(GetActorLocation(), MirrorSnap.WorldLocation);
	}

	// Follow mirror drone Actor directly (not via Registry) to get the anchor-resolved position.
	if (bFollowingMirror && !bIsPaused)
	{
		AActor* MirrorActor = Registry->GetReceiverActor(DroneId);
		if (MirrorActor && IsValid(MirrorActor))
		{
			SetActorLocation(MirrorActor->GetActorLocation());
		}
		return;
	}

	if (bInAssemblyMode && !bIsPaused && MirrorSnap.Availability == EDroneAvailability::Online)
	{
		AActor* MirrorActor = Registry->GetReceiverActor(DroneId);
		if (MirrorActor && IsValid(MirrorActor))
		{
			const FVector NewPos = FMath::VInterpTo(GetActorLocation(), MirrorActor->GetActorLocation(), DeltaTime, AssemblyFollowInterpSpeed);
			SetActorLocation(NewPos);
		}
	}

	// Periodic WebSocket move command (10 Hz) while moving toward a target
	if (bSendClickTarget && !bIsPaused)
	{
		WsSendTimer += DeltaTime;
		if (WsSendTimer >= SendInterval)
		{
			WsSendTimer = 0.0f;
			SendWebSocketMoveCommand();
		}
	}
	else
	{
		WsSendTimer = 0.0f;
	}
}

void AMultiDroneCharacter::EnterAssemblyMode()
{
	if (bInAssemblyMode)
	{
		return;
	}

	bInAssemblyMode = true;
	bSendClickTarget = false;

	if (Registry)
	{
		Registry->ApplyControlLock(DroneId, EDroneControlLockReason::FormationPlayback);
	}

	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter %s: Entered assembly mode"), *DroneName);
}

void AMultiDroneCharacter::ExitAssemblyMode()
{
	if (!bInAssemblyMode)
	{
		return;
	}

	bInAssemblyMode = false;

	if (Registry)
	{
		Registry->ReleaseControlLock(DroneId);
	}

	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter %s: Exited assembly mode"), *DroneName);
}

void AMultiDroneCharacter::SubscribeToAssemblyEvents()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr)
	{
		return;
	}

	NetMgr->OnAssemblingProgress.AddUObject(this, &AMultiDroneCharacter::OnAssemblingProgress);
	NetMgr->OnAssemblyComplete.AddUObject(this, &AMultiDroneCharacter::OnAssemblyComplete);
	NetMgr->OnAssemblyTimeout.AddUObject(this, &AMultiDroneCharacter::OnAssemblyTimeout);
}

void AMultiDroneCharacter::OnAssemblingProgress(const FString& ArrayId, int32 ReadyCount, int32 TotalCount)
{
	EnterAssemblyMode();
}

void AMultiDroneCharacter::OnAssemblyComplete(const FString& ArrayId)
{
	ExitAssemblyMode();
}

void AMultiDroneCharacter::OnAssemblyTimeout(const FString& ArrayId, int32 ReadyCount, int32 TotalCount)
{
	ExitAssemblyMode();
}

void AMultiDroneCharacter::SetPaused(bool bPause)
{
	bIsPaused = bPause;
	if (bPause)
	{
		bWasMovingBeforePause = bSendClickTarget;
		bSendClickTarget = false;
	}
	else
	{
		bSendClickTarget = bWasMovingBeforePause;
	}
	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter %s: %s"), *DroneName, bPause ? TEXT("Paused") : TEXT("Resumed"));
}

void AMultiDroneCharacter::OnPrimarySelected_Implementation()
{
	if (SelectionComponent)
	{
		SelectionComponent->SetPrimarySelected(true);
		SelectionComponent->SetSecondarySelected(false);
	}
	if (SelectionWidgetComponent)
	{
		SelectionWidgetComponent->SetVisibility(true);
	}
}

void AMultiDroneCharacter::OnSecondarySelected_Implementation(bool bSelected)
{
	if (SelectionComponent)
	{
		SelectionComponent->SetSecondarySelected(bSelected);
	}
}

void AMultiDroneCharacter::OnHoveredChanged_Implementation(bool bHovered)
{
	if (SelectionComponent)
	{
		SelectionComponent->SetHovered(bHovered);
	}
}

void AMultiDroneCharacter::OnDeselected_Implementation()
{
	if (SelectionComponent)
	{
		SelectionComponent->SetPrimarySelected(false);
		SelectionComponent->SetSecondarySelected(false);
	}
	if (SelectionWidgetComponent)
	{
		SelectionWidgetComponent->SetVisibility(false);
	}
}

void AMultiDroneCharacter::SetClickTargetLocation(FVector TargetLocation, int32 Mode)
{
	if (bInAssemblyMode || bIsPaused)
	{
		return;
	}

	bFollowingMirror = false;

	ClickTargetLocation = TargetLocation;
	ClickTargetMode = Mode;
	bSendClickTarget = true;
	ClickSendTimer = ClickSendInterval;

	SendWebSocketMoveCommand();
}

void AMultiDroneCharacter::SendWebSocketMoveCommand()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr || !NetMgr->GetWebSocketClient() || !NetMgr->GetWebSocketClient()->IsConnected())
	{
		return;
	}

	FVector SendLocation = ClickTargetLocation;
	if (Registry)
	{
		if (AActor* ReceiverActor = Registry->GetReceiverActor(DroneId))
		{
			if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(ReceiverActor))
			{
				if (Receiver->bHasGpsAnchor)
				{
					SendLocation = ClickTargetLocation - Receiver->AnchorWorldLocation;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("MultiDroneCharacter [%s]: No GPS anchor yet, move command may be incorrect"), *DroneName);
				}
			}
		}
	}

	const EDroneCommandMode CommandMode = Registry
		? Registry->GetDroneCommandMode(DroneId)
		: EDroneCommandMode::Move;

	NetMgr->SendMoveCommand(DroneId, SendLocation, CommandMode);
	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter [%s]: WS move cmd offset=(%.0f, %.0f, %.0f)"),
		*DroneName, SendLocation.X, SendLocation.Y, SendLocation.Z);
}

void AMultiDroneCharacter::StopClickTargetSending()
{
	bSendClickTarget = false;
}

void AMultiDroneCharacter::OnDroneWsEvent(int32 InDroneId, const FString& Event, double GpsLat, double GpsLon, double GpsAlt)
{
	if (InDroneId != DroneId)
	{
		return;
	}

	if (Event != TEXT("power_on") && Event != TEXT("reconnect"))
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UDroneRegistrySubsystem* LocalRegistry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!LocalRegistry)
	{
		return;
	}

	AActor* Receiver = LocalRegistry->GetReceiverActor(DroneId);
	if (!Receiver)
	{
		UE_LOG(LogTemp, Warning, TEXT("MultiDroneCharacter [%s]: '%s' event — no ReceiverActor registered for DroneId=%d"),
			*DroneName, *Event, DroneId);
		return;
	}

	// Reset to mirror-following mode; Tick will continuously sync position from now on.
	bFollowingMirror = true;
	bSendClickTarget = false;

	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter [%s]: '%s' event — now following mirror drone"), *DroneName, *Event);
}
