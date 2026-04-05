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
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"

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

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
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
		NewHoveredId = ResolveDroneIdFromActor(HoveredActor);
	}

	if (HoveredDroneActor.Get() != HoveredActor || HoveredDroneId != NewHoveredId)
	{
		SetDroneHoveredState(HoveredDroneActor.Get(), false);

		if (NewHoveredId > 0 && HoveredActor)
		{
			SetDroneHoveredState(HoveredActor, true);
		}

		HoveredDroneId = NewHoveredId;
		HoveredDroneActor = HoveredActor;
	}
}

void ADroneOpsPlayerController::OnPrimaryClick()
{
	UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: OnPrimaryClick"));
	FVector WorldLocation = FVector::ZeroVector;

	// 1) 优先使用光标下精确命中
	AActor* DroneActor = GetSelectableDroneUnderCursor(&WorldLocation);

	// 2) 精确命中失败时，再用小范围屏幕邻近兜底（避免粘住当前已选中目标）
	if (!DroneActor)
	{
		AActor* Candidate = FindNearestSelectableDroneOnScreen(140.0f);
		if (Candidate)
		{
			DroneActor = Candidate;
		}
	}

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
	if (HoveredDroneId > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Show info for DroneId: %d"), HoveredDroneId);
	}
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
	if (GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
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
	FHitResult PawnHit;
	if (GetHitResultUnderCursor(ECC_Pawn, false, PawnHit))
	{
		if (OutFallbackWorldLocation)
		{
			*OutFallbackWorldLocation = PawnHit.Location;
		}

		AActor* PawnActor = PawnHit.GetActor();
		if (PawnActor)
		{
			UE_LOG(LogTemp, Log, TEXT("Cursor PawnHit: %s (%s)"), *PawnActor->GetName(), *PawnActor->GetClass()->GetName());
		}
		if (IsFr2ControllableDroneActor(PawnActor))
		{
			return PawnActor;
		}
	}

	FHitResult VisibilityHit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, VisibilityHit))
	{
		return nullptr;
	}

	if (OutFallbackWorldLocation)
	{
		*OutFallbackWorldLocation = VisibilityHit.Location;
	}

	AActor* VisibleActor = VisibilityHit.GetActor();
	if (VisibleActor)
	{
		UE_LOG(LogTemp, Log, TEXT("Cursor VisibilityHit: %s (%s)"), *VisibleActor->GetName(), *VisibleActor->GetClass()->GetName());
	}
	if (IsFr2ControllableDroneActor(VisibleActor))
	{
		return VisibleActor;
	}

	const float ClickRadius = 200.0f;
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetPawn());

	if (!GetWorld()->OverlapMultiByChannel(
		Overlaps,
		VisibilityHit.Location,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(ClickRadius),
		QueryParams))
	{
		return nullptr;
	}

	float BestDist = ClickRadius + 1.0f;
	AActor* ClosestDroneActor = nullptr;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* OverlapActor = Overlap.GetActor();
		if (!IsFr2ControllableDroneActor(OverlapActor))
		{
			continue;
		}

		const float Dist = FVector::Dist(VisibilityHit.Location, OverlapActor->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			ClosestDroneActor = OverlapActor;
		}
	}

	return ClosestDroneActor;
}

AActor* ADroneOpsPlayerController::FindNearestSelectableDroneOnScreen(float MaxScreenDistance) const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	float CursorX = 0.0f;
	float CursorY = 0.0f;
	if (!GetMousePosition(CursorX, CursorY))
	{
		return nullptr;
	}

	AActor* BestActor = nullptr;
	float BestDistanceSq = MaxScreenDistance * MaxScreenDistance;

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsFr2ControllableDroneActor(Actor))
		{
			continue;
		}

		FVector2D ScreenPosition;
		const bool bProjected = ProjectWorldLocationToScreen(Actor->GetActorLocation(), ScreenPosition, true);
		if (!bProjected)
		{
			continue;
		}

		const float DistanceSq = FVector2D::DistSquared(ScreenPosition, FVector2D(CursorX, CursorY));
		if (DistanceSq <= BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestActor = Actor;
		}
	}

	if (BestActor)
	{
		UE_LOG(LogTemp, Log, TEXT("Nearest selectable actor: %s (%s) DistSq=%.1f"),
			*BestActor->GetName(), *BestActor->GetClass()->GetName(), BestDistanceSq);
	}

	return BestActor;
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
