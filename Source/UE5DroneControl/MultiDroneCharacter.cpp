// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiDroneCharacter.h"
#include "DroneOps/Drone/DroneSelectionComponent.h"
#include "DroneOps/Drone/DroneCommandSenderComponent.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"

AMultiDroneCharacter::AMultiDroneCharacter()
{
	SelectionComponent = CreateDefaultSubobject<UDroneSelectionComponent>(TEXT("SelectionComponent"));
	CommandSenderComponent = CreateDefaultSubobject<UDroneCommandSenderComponent>(TEXT("CommandSenderComponent"));

	// 确保碰撞设置正确，支持鼠标悬停检测
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// 保持 Pawn 配置，但确保 Visibility 通道阻塞（鼠标检测需要）
		Capsule->SetCollisionProfileName(TEXT("Pawn"));
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		// 【关键】确保 Visibility 通道阻塞 - 鼠标悬停检测需要
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Capsule->SetSimulatePhysics(false);
	}
}

void AMultiDroneCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Sync selection component DroneId
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

	// Register with DroneRegistrySubsystem
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry)
	{
		return;
	}

	// Build descriptor and register
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

	// Subscribe to power_on / reconnect events to sync position with mirror drone
	if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
	{
		NetMgr->OnDroneWsEvent.AddUObject(this, &AMultiDroneCharacter::OnDroneWsEvent);
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
	ClickTargetLocation = TargetLocation;
	ClickTargetMode = Mode;
	bSendClickTarget = true;
	ClickSendTimer = ClickSendInterval;
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

	UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>();
	if (!Registry)
	{
		return;
	}

	// Move shadow drone to the mirror drone's current world position
	AActor* Receiver = Registry->GetReceiverActor(DroneId);
	if (!Receiver)
	{
		UE_LOG(LogTemp, Warning, TEXT("MultiDroneCharacter [%s]: '%s' event — no ReceiverActor registered for DroneId=%d"),
			*DroneName, *Event, DroneId);
		return;
	}

	FVector ReceiverLocation = Receiver->GetActorLocation();
	SetActorLocation(ReceiverLocation);

	UE_LOG(LogTemp, Log, TEXT("MultiDroneCharacter [%s]: '%s' event — synced to mirror drone at %s"),
		*DroneName, *Event, *ReceiverLocation.ToString());
}
