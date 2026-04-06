// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsPlayerController.h"
#include "InputCoreTypes.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/ICoordinateService.h"
#include "DroneOps/Interfaces/DroneSelectableInterface.h"
#include "DroneOps/Drone/DroneSelectionComponent.h"
#include "DroneOps/Drone/DroneCommandSenderComponent.h"
#include "MultiDroneCharacter.h"
#include "RealTimeDroneReceiver.h"
#include "MultiDroneManager.h"
#include "UE5DroneControlCharacter.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/MeshComponent.h"

namespace
{
	bool IsFr2ControllableDroneActor(const AActor* Actor)
	{
		if (!Actor || Actor->IsA(ARealTimeDroneReceiver::StaticClass()))
		{
			return false;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			return true;
		}

		return Actor->FindComponentByClass<UDroneSelectionComponent>() != nullptr;
	}

	int32 ResolveDroneIdFromActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return 0;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			return IDroneSelectableInterface::Execute_GetDroneId(const_cast<AActor*>(Actor));
		}

		if (const UDroneSelectionComponent* SelectionComp = Actor->FindComponentByClass<UDroneSelectionComponent>())
		{
			return SelectionComp->DroneId;
		}

		return 0;
	}

	void SetDroneHoveredState(AActor* Actor, bool bHovered)
	{
		if (!Actor)
		{
			return;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			IDroneSelectableInterface::Execute_OnHoveredChanged(Actor, bHovered);
			return;
		}

		if (UDroneSelectionComponent* SelectionComp = Actor->FindComponentByClass<UDroneSelectionComponent>())
		{
			SelectionComp->SetHovered(bHovered);
		}
	}

	void SetDronePrimarySelectedState(AActor* Actor, bool bSelected)
	{
		if (!Actor)
		{
			return;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			if (bSelected)
			{
				IDroneSelectableInterface::Execute_OnPrimarySelected(Actor);
			}
			else
			{
				IDroneSelectableInterface::Execute_OnDeselected(Actor);
			}
			return;
		}

		if (UDroneSelectionComponent* SelectionComp = Actor->FindComponentByClass<UDroneSelectionComponent>())
		{
			SelectionComp->SetPrimarySelected(bSelected);
			if (!bSelected)
			{
				SelectionComp->SetSecondarySelected(false);
			}
		}
	}
}

ADroneOpsPlayerController::ADroneOpsPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	PrimaryActorTick.bCanEverTick = true;

	// Set default paths to the widgets
	// These are the expected default paths in your content browser
	static ConstructorHelpers::FClassFinder<UUserWidget> InfoPanelFinder(TEXT("/Game/DroneOps/UI/WBP_DroneInfoPanel"));
	if (InfoPanelFinder.Succeeded())
	{
		DroneInfoPanelWidgetClass = InfoPanelFinder.Class;
	}

	static ConstructorHelpers::FClassFinder<UUserWidget> HUDFinder(TEXT("/Game/DroneOps/UI/WBP_DroneOpsHUD"));
	if (HUDFinder.Succeeded())
	{
		DroneOpsHUDWidgetClass = HUDFinder.Class;
	}
}

void ADroneOpsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, TEXT(">>> DroneOpsPlayerController ACTIVE <<<"));
	}
	UE_LOG(LogTemp, Warning, TEXT("=== DroneOpsPlayerController::BeginPlay CALLED ==="));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		DroneRegistry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
		if (DroneRegistry)
		{
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: Connected to DroneRegistry"));
		}
	}

	CameraModeState.CameraMode = EDroneCameraMode::Follow;
	CameraModeState.FollowDroneId = 0;

	// Create and add main HUD widget to viewport
	if (DroneOpsHUDWidgetClass && IsLocalController())
	{
		UUserWidget* HUDWidget = CreateWidget(this, DroneOpsHUDWidgetClass);
		if (HUDWidget)
		{
			HUDWidget->AddToViewport();
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: HUD widget created and added to viewport"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DroneOpsPlayerController: Failed to create HUD widget"));
		}
	}
}

void ADroneOpsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ADroneOpsPlayerController::OnPrimaryClick);
		InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ADroneOpsPlayerController::OnShowInfo);
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ADroneOpsPlayerController::OnFreeCamToggle);
		UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: Input bindings installed"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsPlayerController: InputComponent is null"));
	}
}

void ADroneOpsPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


	int32 NewHoveredId = 0;
	AActor* HoveredActor = GetSelectableDroneUnderCursor();
	if (HoveredActor)
	{
		// Debug: log what actor we hit
		UE_LOG(LogTemp, Verbose, TEXT("[FR-03] Hovered actor: %s (class: %s)"),
			*HoveredActor->GetName(), *HoveredActor->GetClass()->GetName());

		// Check if actor implements IDroneSelectable (which means it's a drone)
		if (Cast<IDroneSelectableInterface>(HoveredActor) != nullptr)
		{
			// For BlueprintNativeEvent functions, must use Execute_* static call
			HoveredDroneId = IDroneSelectableInterface::Execute_GetDroneId(HoveredActor);
			UE_LOG(LogTemp, Log, TEXT("[FR-03] Hovered drone detected, DroneId: %d"), HoveredDroneId);
		}
		else
		{
			HoveredDroneId = 0;
			UE_LOG(LogTemp, Verbose, TEXT("[FR-03] Hovered actor doesn't implement IDroneSelectable: %s"),
				*HoveredActor->GetName());
		}
	}
	else
	{
		HoveredDroneId = 0;
	}
}

void ADroneOpsPlayerController::OnPrimaryClick()
{
	UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: OnPrimaryClick"));
	FVector WorldLocation = FVector::ZeroVector;

	// 只接受“射线精确命中无人机网格体”的点击
	AActor* DroneActor = GetSelectableDroneUnderCursor(&WorldLocation);

	if (GEngine)
	{
		if (DroneActor)
		{
			const int32 Id = IDroneSelectableInterface::Execute_GetDroneId(DroneActor);
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
				FString::Printf(TEXT("DRONE FOUND: %s (ID=%d)"), *DroneActor->GetName(), Id));
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
				FString::Printf(TEXT("Map click at (%.0f, %.0f, %.0f) - no drone nearby"),
					WorldLocation.X, WorldLocation.Y, WorldLocation.Z));
		}
	}

	if (DroneActor)
	{
		HandleDroneClick(DroneActor);
		return;
	}

	if (WorldLocation.IsNearlyZero() && !GetWorldLocationUnderCursor(WorldLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("OnPrimaryClick: Ray miss - no hit under cursor"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Ray miss - no hit under cursor"));
		}
		return;
	}

	HandleMapClick(WorldLocation);
}

void ADroneOpsPlayerController::OnShowInfo()
{
	// Debug: re-check hover state immediately when middle click is pressed
	AActor* HoveredActor = GetActorUnderCursor();
	if (!HoveredActor)
	{
		UE_LOG(LogTemp, Log, TEXT("[FR-03] Middle click - GetActorUnderCursor returned nullptr, cached HoveredDroneId=%d"), HoveredDroneId);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[FR-03] Middle click - Hit actor: %s, class: %s"), *HoveredActor->GetName(), *HoveredActor->GetClass()->GetName());

		bool bImplements = HoveredActor->Implements<UDroneSelectableInterface>();
		if (!bImplements)
		{
			UE_LOG(LogTemp, Log, TEXT("[FR-03] Actor doesn't implement UDroneSelectableInterface"));
		}
		else
		{
			int32 CurrentDroneId = IDroneSelectableInterface::Execute_GetDroneId(HoveredActor);
			UE_LOG(LogTemp, Log, TEXT("[FR-03] Got DroneId: %d from %s"), CurrentDroneId, *HoveredActor->GetName());
			if (CurrentDroneId > 0)
			{
				OpenDroneInfoPanel(CurrentDroneId);
				return;
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[FR-03] Middle click - no drone under cursor, cached HoveredDroneId=%d"), HoveredDroneId);
}

void ADroneOpsPlayerController::OpenDroneInfoPanel(int32 DroneId)
{
	if (!DroneRegistry || !DroneInfoPanelWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] OpenDroneInfoPanel: DroneRegistry=%p, WidgetClass=%p"),
			DroneRegistry.Get(), DroneInfoPanelWidgetClass.Get());
		return;
	}

	// Close existing panel if any
	if (CurrentDroneInfoPanel && CurrentDroneInfoPanel->IsValidLowLevel())
	{
		CurrentDroneInfoPanel->RemoveFromParent();
		CurrentDroneInfoPanel = nullptr;
	}

	// Get telemetry snapshot
	FDroneTelemetrySnapshot Snapshot;
	bool bGotTelemetry = DroneRegistry->GetTelemetry(DroneId, Snapshot);
	UE_LOG(LogTemp, Log, TEXT("[FR-03] GetTelemetry for DroneId=%d, result=%d"), DroneId, bGotTelemetry);

	// Get drone descriptor
	FDroneDescriptor Descriptor;
	bool bGotDescriptor = DroneRegistry->GetDroneDescriptor(DroneId, Descriptor);
	UE_LOG(LogTemp, Log, TEXT("[FR-03] GetDroneDescriptor for DroneId=%d, name=%s, result=%d"),
		DroneId, *Descriptor.Name, bGotDescriptor);

	// Create widget instance
	UUserWidget* NewPanel = CreateWidget(this, DroneInfoPanelWidgetClass);
	if (!NewPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] Failed to create info panel widget"));
		return;
	}

	// Call UpdateFromSnapshot on the widget
	// The function is BlueprintCallable, so we can call it via reflection
	UFunction* Func = NewPanel->FindFunction(FName("UpdateFromSnapshot"));
	if (Func)
	{
		// Prepare parameters: struct Snapshot + string DroneName
		TArray<uint8> ParamsBuffer;
		ParamsBuffer.SetNum(Func->ParmsSize);

		// First parameter is Snapshot
		FDroneTelemetrySnapshot* pSnapshot = reinterpret_cast<FDroneTelemetrySnapshot*>(ParamsBuffer.GetData());
		*pSnapshot = Snapshot;

		// Second parameter is DroneName (FString)
		FString* pName = reinterpret_cast<FString*>(ParamsBuffer.GetData() + sizeof(FDroneTelemetrySnapshot));
		*pName = Descriptor.Name;

		// Call the function
		NewPanel->ProcessEvent(Func, ParamsBuffer.GetData());
		UE_LOG(LogTemp, Log, TEXT("[FR-03] Called UpdateFromSnapshot with DroneId=%d, name=%s"), DroneId, *Descriptor.Name);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] UpdateFromSnapshot function not found on widget"));
	}

	// Add to viewport at top-left
	NewPanel->AddToViewport();
	NewPanel->SetPositionInViewport(FVector2D(10, 10));

	// Save reference
	CurrentDroneInfoPanel = NewPanel;
	UE_LOG(LogTemp, Log, TEXT("[FR-03] Info panel opened successfully for DroneId=%d"), DroneId);
}

void ADroneOpsPlayerController::OnFreeCamToggle()
{
	if (CameraModeState.CameraMode == EDroneCameraMode::Follow)
	{
		CameraModeState.CameraMode = EDroneCameraMode::Free;
		UE_LOG(LogTemp, Log, TEXT("Switched to Free Camera"));
	}
	else
	{
		CameraModeState.CameraMode = EDroneCameraMode::Follow;
		UE_LOG(LogTemp, Log, TEXT("Switched to Follow Camera"));
	}
}

void ADroneOpsPlayerController::HandleMapClick(const FVector& WorldLocation)
{
	if (!SelectedDroneActor || SelectedDroneId <= 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, TEXT("未选中无人机，无法派发目标点"));
		}
		return;
	}

	if (DroneRegistry)
	{
		EDroneControlLockReason LockReason = EDroneControlLockReason::None;
		if (DroneRegistry->IsControlLocked(SelectedDroneId, LockReason))
		{
			UE_LOG(LogTemp, Warning, TEXT("Drone %d is locked, map target ignored"), SelectedDroneId);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("当前无人机不可控，目标点派发失败"));
			}
			return;
		}
	}

	AActor* TargetActor = SelectedDroneActor.Get();
	if (!IsValid(TargetActor))
	{
		TargetActor = ResolveDroneActorById(SelectedDroneId);
		SelectedDroneActor = TargetActor;
	}

	int32 EffectiveDroneId = SelectedDroneId;
	if (TargetActor)
	{
		const int32 ActorDroneId = ResolveDroneIdFromActor(TargetActor);
		if (ActorDroneId > 0)
		{
			EffectiveDroneId = ActorDroneId;
			SelectedDroneId = ActorDroneId;
		}
	}

	// 本地可视移动优先使用“当前实际选中的Actor”，避免Registry映射错位导致点A动B
	if (AUE5DroneControlCharacter* SelectedChar = Cast<AUE5DroneControlCharacter>(TargetActor))
	{
		SelectedChar->SetClickTargetLocation(WorldLocation, 1);
	}

	UE_LOG(LogTemp, Log, TEXT("FR2 Dispatch: Actor=%s SelectedDroneId=%d EffectiveDroneId=%d"),
		TargetActor ? *TargetActor->GetName() : TEXT("None"), SelectedDroneId, EffectiveDroneId);
	SendTargetCommand(EffectiveDroneId, WorldLocation);

	const FString TargetName = TargetActor ? TargetActor->GetName() : FString::Printf(TEXT("Drone-%d"), SelectedDroneId);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			FString::Printf(TEXT("目标点已派发 -> %s (%.0f, %.0f, %.0f)"),
				*TargetName, WorldLocation.X, WorldLocation.Y, WorldLocation.Z));
	}

	DrawDebugSphere(GetWorld(), WorldLocation, 50.0f, 12, FColor::Green, false, 3.0f);
}

void ADroneOpsPlayerController::HandleDroneClick(AActor* ClickedActor)
{
	if (!ClickedActor)
	{
		return;
	}

	if (!IsFr2ControllableDroneActor(ClickedActor))
	{
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			FString::Printf(TEXT("Selected: %s - click map to assign target"), *ClickedActor->GetName()));
	}

	if (!DroneRegistry)
	{
		SelectedDroneId = 0;
		SelectedDroneActor = ClickedActor;
		return;
	}

	const int32 ClickedDroneId = ResolveDroneIdFromActor(ClickedActor);
	if (ClickedDroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Clicked drone has invalid DroneId: %s"), *ClickedActor->GetName());
		return;
	}

	EDroneControlLockReason LockReason = EDroneControlLockReason::None;
	if (DroneRegistry->IsControlLocked(ClickedDroneId, LockReason) &&
		LockReason == EDroneControlLockReason::Offline)
	{
		UE_LOG(LogTemp, Warning, TEXT("Drone %d is offline, cannot select"), ClickedDroneId);
		return;
	}

	if (SelectedDroneActor && SelectedDroneActor != ClickedActor)
	{
		SetDronePrimarySelectedState(SelectedDroneActor.Get(), false);
	}

	SelectedDroneId = ClickedDroneId;
	SelectedDroneActor = ClickedActor;

	DroneRegistry->SetPrimarySelectedDrone(ClickedDroneId);
	SetDronePrimarySelectedState(ClickedActor, true);

	FDroneDescriptor Desc;
	FString DisplayName = FString::Printf(TEXT("Drone-%d"), ClickedDroneId);
	if (DroneRegistry->GetDroneDescriptor(ClickedDroneId, Desc))
	{
		DisplayName = Desc.Name;
	}

	UE_LOG(LogTemp, Log, TEXT("Primary selected: %s (ID=%d)"), *DisplayName, ClickedDroneId);
}

void ADroneOpsPlayerController::SendTargetCommand(int32 DroneId, const FVector& TargetWorldLocation)
{
	if (!DroneRegistry)
	{
		return;
	}

	TScriptInterface<ICoordinateService> CoordService = DroneRegistry->GetCoordinateService();
	if (!CoordService.GetObject())
	{
		UE_LOG(LogTemp, Error, TEXT("No coordinate service available"));
		return;
	}

	const FVector NedLocation = ICoordinateService::Execute_WorldToNed(CoordService.GetObject(), TargetWorldLocation);

	FDroneTargetCommand Command;
	Command.DroneId = DroneId;
	Command.TargetWorldLocation = TargetWorldLocation;
	Command.TargetNedLocation = NedLocation;
	Command.Mode = 1;
	Command.IssuedAt = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Log, TEXT("Target command for Drone %d: World=(%.1f, %.1f, %.1f) NED=(%.2f, %.2f, %.2f)"),
		DroneId,
		TargetWorldLocation.X, TargetWorldLocation.Y, TargetWorldLocation.Z,
		NedLocation.X, NedLocation.Y, NedLocation.Z);

	UDroneCommandSenderComponent* CommandSender = nullptr;
	TArray<AActor*> Managers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMultiDroneManager::StaticClass(), Managers);
	if (Managers.Num() > 0)
	{
		CommandSender = Managers[0]->FindComponentByClass<UDroneCommandSenderComponent>();
	}

	if (CommandSender)
	{
		CommandSender->SendSingleDroneCommand(DroneId, NedLocation, Command.Mode);
		UE_LOG(LogTemp, Log, TEXT("Command dispatched via DroneCommandSenderComponent for Drone %d"), DroneId);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("No command sender available for Drone %d"), DroneId);
}

AActor* ADroneOpsPlayerController::GetActorUnderCursor() const
{
	FHitResult HitResult;
	if (GetHitResultUnderCursor(ECC_Visibility, true, HitResult))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FR-03] Ray trace hit: %s at (%.1f, %.1f, %.1f)"),
			HitResult.GetActor() ? *HitResult.GetActor()->GetName() : TEXT("NULL"),
			HitResult.Location.X, HitResult.Location.Y, HitResult.Location.Z);
		return HitResult.GetActor();
	}
	return nullptr;
}

bool ADroneOpsPlayerController::GetWorldLocationUnderCursor(FVector& OutLocation) const
{
	FHitResult HitResult;
	if (GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		OutLocation = HitResult.Location;
		return true;
	}
	return false;
}

AActor* ADroneOpsPlayerController::GetSelectableDroneUnderCursor(FVector* OutFallbackWorldLocation) const
{
	FHitResult VisibilityHit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, VisibilityHit))
	{
		return nullptr;
	}

	if (OutFallbackWorldLocation)
	{
		*OutFallbackWorldLocation = VisibilityHit.Location;
	}

	AActor* HitActor = VisibilityHit.GetActor();
	UPrimitiveComponent* HitComponent = VisibilityHit.GetComponent();
	if (!HitActor || !HitComponent)
	{
		return nullptr;
	}

	if (!IsFr2ControllableDroneActor(HitActor))
	{
		return nullptr;
	}

	// 仅当射线命中“无人机网格体组件”时才算选中，避免点到空白区域也选中
	if (!HitComponent->IsA<UMeshComponent>())
	{
		return nullptr;
	}

	UE_LOG(LogTemp, Verbose, TEXT("Cursor MeshHit: %s.%s"), *HitActor->GetName(), *HitComponent->GetName());
	return HitActor;
}

AActor* ADroneOpsPlayerController::FindNearestSelectableDroneOnScreen(float MaxScreenDistance) const
{
	return nullptr;
}

AActor* ADroneOpsPlayerController::ResolveDroneActorById(int32 DroneId) const
{
	if (!DroneRegistry || DroneId <= 0)
	{
		return nullptr;
	}

	if (APawn* SenderPawn = DroneRegistry->GetSenderPawn(DroneId))
	{
		if (IsFr2ControllableDroneActor(SenderPawn))
		{
			return SenderPawn;
		}
	}

	AActor* ReceiverActor = DroneRegistry->GetReceiverActor(DroneId);
	return IsFr2ControllableDroneActor(ReceiverActor) ? ReceiverActor : nullptr;
}
