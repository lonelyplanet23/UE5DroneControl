// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOps/Drone/DroneSelectionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

UDroneSelectionComponent::UDroneSelectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	static const TCHAR* DefaultHighlightMatPath = TEXT("/Game/DroneOps/Materials/M_DroneHighlight.M_DroneHighlight");
	if (UMaterialInterface* DefaultHighlightMat = LoadObject<UMaterialInterface>(nullptr, DefaultHighlightMatPath))
	{
		PrimarySelectedOverlayMaterial = DefaultHighlightMat;
		HoveredOverlayMaterial = DefaultHighlightMat;
	}
}

void UDroneSelectionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		OriginalActorScale = Owner->GetActorScale3D();
		bHasCapturedOriginalScale = true;
	}

	EnsureSelectionLabel();
	ApplyVisualState();
}

void UDroneSelectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!bEnableDebugDrawHighlight)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		FVector BoundsOrigin = FVector::ZeroVector;
		FVector BoundsExtent = FVector::ZeroVector;
		Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);
		const float AutoRadius = BoundsExtent.Size() * 0.65f;
		const float Radius = FMath::Max(DebugHighlightRadius, AutoRadius);

		if (bIsPrimarySelected)
		{
			DrawDebugSphere(World, BoundsOrigin, Radius, 32, FColor::Yellow, false, 0.12f, 0, DebugHighlightThickness);
		}
		else if (bIsHovered)
		{
			DrawDebugSphere(World, BoundsOrigin, Radius, 24, FColor::Green, false, 0.12f, 0, DebugHighlightThickness * 0.75f);
		}
	}
}

void UDroneSelectionComponent::SetPrimarySelected(bool bSelected)
{
	bool bChanged = (bIsPrimarySelected != bSelected);
	bIsPrimarySelected = bSelected;
	if (bChanged)
	{
		ApplyVisualState();
		BroadcastIfChanged();
	}
}

void UDroneSelectionComponent::SetSecondarySelected(bool bSelected)
{
	bool bChanged = (bIsSecondarySelected != bSelected);
	bIsSecondarySelected = bSelected;
	if (bChanged)
	{
		ApplyVisualState();
		BroadcastIfChanged();
	}
}

void UDroneSelectionComponent::SetHovered(bool bHovered)
{
	bIsHovered = bHovered;
	ApplyVisualState();
}

void UDroneSelectionComponent::BroadcastIfChanged()
{
	OnSelectionStateChanged.Broadcast(bIsPrimarySelected, bIsSecondarySelected);
}

void UDroneSelectionComponent::ApplyVisualState()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!bHasCapturedOriginalScale)
	{
		OriginalActorScale = Owner->GetActorScale3D();
		bHasCapturedOriginalScale = true;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents(PrimitiveComponents);

	const bool bShowHighlight = bIsPrimarySelected || bIsSecondarySelected || bIsHovered;
	const int32 StencilValue = bIsPrimarySelected ? 252 : (bIsHovered ? 251 : 250);

	Owner->SetActorScale3D(OriginalActorScale);

	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (!Primitive || Primitive->IsVisualizationComponent())
		{
			continue;
		}

		Primitive->SetRenderCustomDepth(bShowHighlight);
		if (bShowHighlight)
		{
			Primitive->SetCustomDepthStencilValue(StencilValue);
		}

		if (UMeshComponent* MeshComp = Cast<UMeshComponent>(Primitive))
		{
			UMaterialInterface* OverlayMaterial = nullptr;
			if (bIsPrimarySelected)
			{
				OverlayMaterial = PrimarySelectedOverlayMaterial;
			}
			else if (bIsHovered)
			{
				OverlayMaterial = HoveredOverlayMaterial ? HoveredOverlayMaterial : PrimarySelectedOverlayMaterial;
			}
			MeshComp->SetOverlayMaterial(OverlayMaterial);
		}
	}

	UpdateSelectionLabelVisibility();

	if (SelectionPointLightComponent)
	{
		const bool bShowSelectedLight = bIsPrimarySelected || bIsSecondarySelected || bIsHovered;
		const float TargetIntensity = bIsPrimarySelected
			? SelectedLightIntensity
			: (bIsHovered ? HoveredLightIntensity : SelectedLightIntensity * 0.6f);
		SelectionPointLightComponent->SetIntensity(bShowSelectedLight ? TargetIntensity : 0.0f);
		SelectionPointLightComponent->SetAttenuationRadius(HighlightLightRadius);
		SelectionPointLightComponent->SetLightColor(HighlightLightColor.ToFColorSRGB());
	}

}

void UDroneSelectionComponent::EnsureSelectionLabel()
{
	AActor* Owner = GetOwner();
	if (!Owner || SelectionLabelComponent)
	{
		return;
	}

	SelectionLabelComponent = NewObject<UTextRenderComponent>(Owner, TEXT("SelectionLabel"));
	if (!SelectionLabelComponent)
	{
		return;
	}

	USceneComponent* AnchorComponent = Owner->GetRootComponent();
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents(PrimitiveComponents);
	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (Primitive && !Primitive->IsVisualizationComponent())
		{
			AnchorComponent = Primitive;
			break;
		}
	}

	SelectionLabelComponent->SetupAttachment(AnchorComponent);
	SelectionLabelComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	SelectionLabelComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	SelectionLabelComponent->SetTextRenderColor(SelectionLabelColor);
	SelectionLabelComponent->SetText(SelectionLabelText);
	SelectionLabelComponent->SetXScale(1.0f);
	SelectionLabelComponent->SetYScale(1.0f);
	SelectionLabelComponent->SetWorldSize(SelectionLabelWorldSize);
	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	const float LabelZ = BoundsExtent.Z + SelectionLabelZOffset;
	SelectionLabelComponent->SetRelativeLocation(FVector(0.0f, 0.0f, LabelZ));
	SelectionLabelComponent->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	SelectionLabelComponent->SetHiddenInGame(true);
	SelectionLabelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionLabelComponent->RegisterComponent();

	SelectionFxComponent = NewObject<UTextRenderComponent>(Owner, TEXT("SelectionFX"));
	if (SelectionFxComponent)
	{
		SelectionFxComponent->SetupAttachment(AnchorComponent);
		SelectionFxComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		SelectionFxComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
		SelectionFxComponent->SetTextRenderColor(SelectionMarkerColor);
		SelectionFxComponent->SetText(SelectionMarkerText);
		SelectionFxComponent->SetXScale(1.0f);
		SelectionFxComponent->SetYScale(1.0f);
		SelectionFxComponent->SetWorldSize(SelectionMarkerWorldSize);
		const float FxZ = LabelZ + SelectionMarkerExtraZOffset;
		SelectionFxComponent->SetRelativeLocation(FVector(0.0f, 0.0f, FxZ));
		SelectionFxComponent->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
		SelectionFxComponent->SetHiddenInGame(true);
		SelectionFxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SelectionFxComponent->RegisterComponent();
	}

	if (bEnablePointLightHighlight)
	{
		SelectionPointLightComponent = NewObject<UPointLightComponent>(Owner, TEXT("SelectionPointLight"));
		if (SelectionPointLightComponent)
		{
			SelectionPointLightComponent->SetupAttachment(AnchorComponent);
			SelectionPointLightComponent->SetLightColor(HighlightLightColor.ToFColorSRGB());
			SelectionPointLightComponent->SetAttenuationRadius(HighlightLightRadius);
			SelectionPointLightComponent->SetIntensity(0.0f);
			SelectionPointLightComponent->SetUseInverseSquaredFalloff(false);
			SelectionPointLightComponent->SetCastShadows(false);
			SelectionPointLightComponent->SetRelativeLocation(FVector(0.0f, 0.0f, LabelZ + HighlightLightExtraZOffset));
			SelectionPointLightComponent->SetHiddenInGame(false);
			SelectionPointLightComponent->RegisterComponent();
		}
	}
}

void UDroneSelectionComponent::UpdateSelectionLabelVisibility()
{
	if (!SelectionLabelComponent)
	{
		return;
	}

	const bool bShowSelectedLabel = bIsPrimarySelected;
	SelectionLabelComponent->SetHiddenInGame(!bShowSelectedLabel);
	if (SelectionFxComponent)
	{
		SelectionFxComponent->SetHiddenInGame(!bShowSelectedLabel);
	}
}
