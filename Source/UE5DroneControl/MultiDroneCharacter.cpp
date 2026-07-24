// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiDroneCharacter.h"
#include "RealTimeDroneReceiver.h"
#include "DroneOps/Drone/DroneSelectionComponent.h"
#include "DroneOps/Drone/DroneCommandSenderComponent.h"
#include "DroneOps/Drone/DroneGroundProjectionComponent.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOps/Network/DroneHttpClient.h"
#include "PathEditor/DronePathActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"

AMultiDroneCharacter::AMultiDroneCharacter()
{
	PrimaryActorTick.bCanEverTick = true;  // remote: Tick 需要  // remote: Tick 需要

	// Actor Z 直接代表真实飞行高度（跟随 Mirror 或点击目标），
	// 禁用父类通过 Mesh 相对 Z 模拟高度的逻辑，保持旋转中心正确。
	bUseMeshHeightOffset = false;

	SelectionComponent = CreateDefaultSubobject<UDroneSelectionComponent>(TEXT("SelectionComponent"));
	CommandSenderComponent = CreateDefaultSubobject<UDroneCommandSenderComponent>(TEXT("CommandSenderComponent"));
	GroundProjectionComponent = CreateDefaultSubobject<UDroneGroundProjectionComponent>(TEXT("GroundProjection"));

	// 确保碰撞设置正确，支持鼠标悬停检测
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionProfileName(TEXT("Pawn"));
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		// Capsule 不阻挡 Visibility——让射线穿透打到 Mesh。
		// 修正高度逻辑后 Mesh 与 Capsule 同高，Capsule 会被先命中；
		// 由 Mesh 接收 Visibility 才能通过 IsA<UMeshComponent>() 选中检测。
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Capsule->SetSimulatePhysics(false);
	}

	// Mesh 负责 Visibility 命中（鼠标悬停 / 点击选中的实际射线接收体）
	if (USkeletalMeshComponent* MyMesh = GetMesh())
	{
		MyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}
}

void AMultiDroneCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 蓝图实例可能保存了旧碰撞配置；运行时再兜底一次，保证鼠标射线能命中影子机。
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	if (USkeletalMeshComponent* MyMesh = GetMesh())
	{
		MyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

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
		// Strict local preview owns the shadow transform for the whole level. Never
		// fall back to a telemetry mirror that is intentionally absent.
		if (NetMgr->IsStrictLocalPreviewIsolation())
		{
			bFollowingMirror = false;
		}

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
		bVerticalMoveActive = false;
		VerticalMoveDirection = 0.0f;
		bOneShot3DMoveActive = false;
	}

	// Disable parent UDP send — shadow drones use WebSocket exclusively
	bEnableUDPSend = false;

	Super::Tick(DeltaTime);

	// An attack takes exclusive local movement ownership until it reaches its target.
	if (bIsLocalAttacking && !bIsLocalAttackCompleted)
	{
		const FVector CurrentPos = GetActorLocation();
		const FVector DirToTarget = AttackTargetLocation - CurrentPos;
		const float DistToTarget = DirToTarget.Size();
		if (DistToTarget <= AttackArrivalThresholdCm)
		{
			SetActorLocation(AttackTargetLocation);
			bIsLocalAttackCompleted = true;
			bIsLocalAttacking = false;
			if (Registry)
			{
				Registry->UpdateLocalState(DroneId, EUELocalDroneState::LocalAttackCompleted);
			}
			UE_LOG(LogTemp, Log, TEXT("[MultiDroneCharacter] %s: Local attack completed at target"), *DroneName);
			return;
		}

		const FVector NewPos = CurrentPos + DirToTarget.GetSafeNormal() * LocalAttackSpeed * DeltaTime;
		SetActorLocation(NewPos);
		if (bOrientToAttackTarget)
		{
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), DirToTarget.Rotation(), DeltaTime, 5.0f));
		}
		if (Registry)
		{
			FDroneTelemetrySnapshot Snap;
			if (Registry->GetTelemetry(DroneId, Snap))
			{
				Snap.WorldLocation = NewPos;
				Snap.LastUpdateTime = FPlatformTime::Seconds();
				Registry->UpdateTelemetry(DroneId, Snap);
			}
		}
		return;
	}

	// One-shot 3D local visual move (geographic dispatch). Honours target Z, unlike the
	// parent XY-only click movement, and drives no WebSocket resend. Runs before the
	// mirror-follow / assembly blocks so those do not fight it back to the mirror position.
	if (bOneShot3DMoveActive && !bIsPaused && !bInAssemblyMode)
	{
		const FVector CurrentLocation = GetActorLocation();
		const FVector NewLocation = FMath::VInterpConstantTo(
			CurrentLocation, OneShot3DTarget, DeltaTime, OneShot3DMoveSpeedCmPerSec);
		SetActorLocation(NewLocation);

		if (FVector::DistSquared(NewLocation, OneShot3DTarget) <= OneShot3DArriveThresholdCm * OneShot3DArriveThresholdCm)
		{
			SetActorLocation(OneShot3DTarget);
			bOneShot3DMoveActive = false;
			UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter [%s]: One-shot 3D move arrived at target"), *DroneName);
		}
		return;
	}

	// Parent click movement intentionally preserves the current Actor Z. Apply
	// shadow-drone vertical movement afterwards so Q/E owns Z independently.
	if (bVerticalMoveActive && !bIsPaused && !bInAssemblyMode)
	{
		FVector NewLocation = GetActorLocation();
		const float AnchorWorldZ = GetVerticalAnchorWorldZ();
		const float MinAllowedHeight = FMath::Min(MinVerticalHeightCm, MaxVerticalHeightCm);
		const float MaxAllowedHeight = FMath::Max(MinVerticalHeightCm, MaxVerticalHeightCm);
		const float NewRelativeZ = FMath::Clamp(
			(NewLocation.Z - AnchorWorldZ) + VerticalMoveDirection * VerticalMoveSpeedCmPerSec * DeltaTime,
			MinAllowedHeight,
			MaxAllowedHeight);
		NewLocation.Z = AnchorWorldZ + NewRelativeZ;
		SetActorLocation(NewLocation);
		RefreshVerticalCommandTarget();
	}

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

	// Use one rate limiter for click and vertical targets. The default 0.5 s
	// interval matches the backend's current 2 Hz command consumption rate.
	if ((bSendClickTarget || bVerticalMoveActive) && !bIsPaused)
	{
		WsSendTimer += DeltaTime;
		const float SafeSendInterval = FMath::Max(WebSocketTargetSendIntervalSec, 0.05f);
		if (WsSendTimer >= SafeSendInterval)
		{
			WsSendTimer = FMath::Fmod(WsSendTimer, SafeSendInterval);
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
	bVerticalMoveActive = false;
	VerticalMoveDirection = 0.0f;
	bOneShot3DMoveActive = false;
	WsSendTimer = 0.0f;

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

void AMultiDroneCharacter::SetLocalPathPreviewActive(bool bActive)
{
	if (bLocalPathPreviewActive == bActive)
	{
		return;
	}

	bLocalPathPreviewActive = bActive;
	if (bActive)
	{
		// A local preview owns this actor's transform. Clear both remote transform
		// writers before DronePathActor begins its movement tick.
		ExitAssemblyMode();
		bFollowingMirror = false;
		bSendClickTarget = false;
		bVerticalMoveActive = false;
		bOneShot3DMoveActive = false;
		WsSendTimer = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter %s: Local path preview enabled; remote assembly events suppressed"), *DroneName);
	}
	else
	{
		const UGameInstance* GI = GetGameInstance();
		const UDroneNetworkManager* NetMgr =
			GI ? GI->GetSubsystem<UDroneNetworkManager>() : nullptr;
		const bool bStrictIsolation = NetMgr && NetMgr->IsStrictLocalPreviewIsolation();

		// Normal preview rejoins authoritative telemetry. Strict isolation has no
		// mirror actor, so retain local transform ownership between preview runs.
		bFollowingMirror = !bStrictIsolation;
		UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter %s: Local path preview ended; mirror follow=%d (isolation=%d)"),
			*DroneName, bFollowingMirror ? 1 : 0, bStrictIsolation ? 1 : 0);
	}
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
	if (bLocalPathPreviewActive)
	{
		UE_LOG(LogTemp, Verbose, TEXT("MultiDroneCharacter %s: Ignoring backend assembling event during local path preview"), *DroneName);
		return;
	}

	EnterAssemblyMode();
}

void AMultiDroneCharacter::OnAssemblyComplete(const FString& ArrayId)
{
	if (bLocalPathPreviewActive)
	{
		return;
	}

	ExitAssemblyMode();
}

void AMultiDroneCharacter::OnAssemblyTimeout(const FString& ArrayId, int32 ReadyCount, int32 TotalCount)
{
	if (bLocalPathPreviewActive)
	{
		return;
	}

	ExitAssemblyMode();
}

void AMultiDroneCharacter::SetPaused(bool bPause)
{
	bIsPaused = bPause;
	if (bPause)
	{
		bWasMovingBeforePause = bSendClickTarget;
		bSendClickTarget = false;
		bVerticalMoveActive = false;
		VerticalMoveDirection = 0.0f;
		bOneShot3DMoveActive = false;
		WsSendTimer = 0.0f;
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
	bOneShot3DMoveActive = false;

	ClickTargetLocation = TargetLocation;
	if (bVerticalMoveActive)
	{
		// A map click may carry ground Z. Keep the height currently owned by Q/E.
		ClickTargetLocation.Z = GetActorLocation().Z;
	}
	ClickTargetMode = Mode;
	bSendClickTarget = true;
	ClickSendTimer = ClickSendInterval;
	if (bVerticalMoveActive)
	{
		RefreshVerticalCommandTarget();
	}

	SendWebSocketMoveCommand();
	WsSendTimer = 0.0f;
}

void AMultiDroneCharacter::MoveToTarget3D(FVector WorldTarget)
{
	if (bInAssemblyMode || bIsPaused)
	{
		return;
	}

	// Take over movement locally without any periodic WebSocket resend: the dispatcher
	// sends the backend command once and the backend owns reliable resend.
	bFollowingMirror = false;
	bSendClickTarget = false;
	bVerticalMoveActive = false;
	VerticalMoveDirection = 0.0f;
	WsSendTimer = 0.0f;

	OneShot3DTarget = WorldTarget;
	bOneShot3DMoveActive = true;
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

	FVector SendLocation = bVerticalMoveActive ? VerticalCommandTargetLocation : ClickTargetLocation;
	if (Registry)
	{
		if (AActor* ReceiverActor = Registry->GetReceiverActor(DroneId))
		{
			if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(ReceiverActor))
			{
				if (Receiver->bHasGpsAnchor)
				{
					const FVector WorldTarget = bVerticalMoveActive ? VerticalCommandTargetLocation : ClickTargetLocation;
					SendLocation = WorldTarget - Receiver->AnchorWorldLocation;
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
	bOneShot3DMoveActive = false;
	if (bVerticalMoveActive)
	{
		const FVector CurrentLocation = GetActorLocation();
		VerticalCommandTargetLocation.X = CurrentLocation.X;
		VerticalCommandTargetLocation.Y = CurrentLocation.Y;
		VerticalCommandTargetLocation.Z = CurrentLocation.Z;
	}
}

void AMultiDroneCharacter::SetVerticalMoveInput(float Direction)
{
	const float ClampedDirection = FMath::Clamp(Direction, -1.0f, 1.0f);
	if (FMath::IsNearlyZero(ClampedDirection))
	{
		StopVerticalMove();
		return;
	}

	if (bInAssemblyMode || bIsPaused)
	{
		return;
	}

	if (!bVerticalMoveActive)
	{
		bVerticalMoveActive = true;
		bFollowingMirror = false;
		bOneShot3DMoveActive = false;
		VerticalCommandTargetLocation = bSendClickTarget ? ClickTargetLocation : GetActorLocation();
		VerticalCommandTargetLocation.Z = GetActorLocation().Z;

		// Make the first updated height eligible for sending on the next Actor Tick.
		WsSendTimer = FMath::Max(WebSocketTargetSendIntervalSec, 0.05f);
	}

	VerticalMoveDirection = ClampedDirection;
}

void AMultiDroneCharacter::StopVerticalMove(bool bSendFinalCommand)
{
	if (!bVerticalMoveActive)
	{
		return;
	}

	RefreshVerticalCommandTarget();
	if (bSendFinalCommand && !bIsPaused)
	{
		SendWebSocketMoveCommand();
	}

	// If XY click movement is still active, keep its future commands at the
	// final Q/E height rather than reverting to the original map-click Z.
	if (bSendClickTarget)
	{
		ClickTargetLocation.Z = VerticalCommandTargetLocation.Z;
	}

	bVerticalMoveActive = false;
	VerticalMoveDirection = 0.0f;
	WsSendTimer = 0.0f;
}

void AMultiDroneCharacter::RefreshVerticalCommandTarget()
{
	if (!bVerticalMoveActive)
	{
		return;
	}

	if (bSendClickTarget)
	{
		VerticalCommandTargetLocation.X = ClickTargetLocation.X;
		VerticalCommandTargetLocation.Y = ClickTargetLocation.Y;
	}
	VerticalCommandTargetLocation.Z = GetActorLocation().Z;
}

float AMultiDroneCharacter::GetVerticalAnchorWorldZ() const
{
	if (Registry)
	{
		if (const ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(Registry->GetReceiverActor(DroneId)))
		{
			if (Receiver->bHasGpsAnchor)
			{
				return Receiver->AnchorWorldLocation.Z;
			}
		}
	}

	return 0.0f;
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

	// Local-only path preview is an intentionally isolated mode. Ignore anchor events so
	// existing preview movement is not overwritten by backend connectivity churn.
	if (bLocalPathPreviewActive)
	{
		UE_LOG(LogTemp, Verbose, TEXT("MultiDroneCharacter [%s]: ignoring '%s' event during local path preview"),
			*DroneName, *Event);
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
	bVerticalMoveActive = false;
	VerticalMoveDirection = 0.0f;
	bOneShot3DMoveActive = false;
	WsSendTimer = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter [%s]: '%s' event — now following mirror drone"), *DroneName, *Event);
}

// 敌对目标攻击

void AMultiDroneCharacter::StopPatrolAndAttack(const FVector& TargetLocation)
{
	if (bIsLocalAttacking || bIsLocalAttackCompleted)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MultiDroneCharacter] %s: Already attacking or completed"),
			*DroneName);
		return;
	}

	// 1. 停止当前巡逻路径
	// 查找当前控制此无人机的 PathActor
	UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<ADronePathActor> It(World); It; ++It)
		{
			ADronePathActor* PathActor = *It;
			if (!IsValid(PathActor))
			{
				continue;
			}

			AActor* ControlledDrone = PathActor->ControlledDrone.Get();
			if (ControlledDrone == this || (IsValid(ControlledDrone) &&
				ControlledDrone->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()) &&
				IDroneSelectableInterface::Execute_GetDroneId(ControlledDrone) == DroneId))
			{
				// 暂停路径移动（不销毁）
				PathActor->PauseMovement();
				CachedPathActor = PathActor;
				UE_LOG(LogTemp, Log, TEXT("[MultiDroneCharacter] %s: Paused path actor %s"),
					*DroneName, *PathActor->GetName());
				break;
			}
		}
	}

	// 2. 停止跟随镜像机
	bFollowingMirror = false;

	// 3. 设置攻击状态
	bIsLocalAttacking = true;
	bIsLocalAttackCompleted = false;
	AttackTargetLocation = TargetLocation;

	// 4. 更新本地状态（面板显示）
	if (Registry)
	{
		Registry->UpdateLocalState(DroneId, EUELocalDroneState::LocalAttacking);
	}

	UE_LOG(LogTemp, Log, TEXT("[MultiDroneCharacter] %s: StopPatrolAndAttack -> target (%.1f, %.1f, %.1f)"),
		*DroneName, TargetLocation.X, TargetLocation.Y, TargetLocation.Z);
}

void AMultiDroneCharacter::ResetLocalAttackState()
{
	bIsLocalAttacking = false;
	bIsLocalAttackCompleted = false;
	AttackTargetLocation = FVector::ZeroVector;

	const bool bResumeLocalPatrol = CachedPathActor.IsValid();
	if (bResumeLocalPatrol)
	{
		CachedPathActor->ResumeMovement();
	}

	if (Registry)
	{
		Registry->UpdateLocalState(DroneId,
			bResumeLocalPatrol ? EUELocalDroneState::LocalPatrolling : EUELocalDroneState::None);
	}

	const UGameInstance* GI = GetGameInstance();
	const UDroneNetworkManager* NetMgr =
		GI ? GI->GetSubsystem<UDroneNetworkManager>() : nullptr;
	const bool bStrictIsolation = NetMgr && NetMgr->IsStrictLocalPreviewIsolation();

	// A paused local patrol resumes from the same path actor. Only return to mirror
	// following when no local owner remains and backend telemetry is allowed.
	bFollowingMirror = !bResumeLocalPatrol && !bLocalPathPreviewActive && !bStrictIsolation;
	CachedPathActor = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[MultiDroneCharacter] %s: Reset local attack state (resumePatrol=%d, mirrorFollow=%d)"),
		*DroneName, bResumeLocalPatrol ? 1 : 0, bFollowingMirror ? 1 : 0);
}
