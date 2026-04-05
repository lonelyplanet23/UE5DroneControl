// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInterface.h"
#include "DroneSelectionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSelectionStateChanged, bool, bIsPrimary, bool, bIsSecondary);

/**
 * Component for drone selection state (primary / secondary / hovered)
 * Attach to any Actor that should be selectable as a drone.
 */
UCLASS(ClassGroup = "DroneOps", meta = (BlueprintSpawnableComponent))
class UE5DRONECONTROL_API UDroneSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDroneSelectionComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// The DroneId this component belongs to
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection")
	int32 DroneId = 0;

	// Theme color for this drone (used by outline / highlight)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection")
	FLinearColor ThemeColor = FLinearColor::White;

	// ---- Selection label / marker config ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label")
	FText SelectionLabelText = FText::FromString(TEXT("已选中"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label")
	FColor SelectionLabelColor = FColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label", meta = (ClampMin = "1.0"))
	float SelectionLabelWorldSize = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label")
	FText SelectionMarkerText = FText::FromString(TEXT("●"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label")
	FColor SelectionMarkerColor = FColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label", meta = (ClampMin = "1.0"))
	float SelectionMarkerWorldSize = 48.0f;

	// 在 Actor 包围盒顶部基础上的额外偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label")
	float SelectionLabelZOffset = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Label")
	float SelectionMarkerExtraZOffset = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight")
	bool bEnablePointLightHighlight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight", meta = (ClampMin = "0.0"))
	float SelectedLightIntensity = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight", meta = (ClampMin = "0.0"))
	float HoveredLightIntensity = 4500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight", meta = (ClampMin = "1.0"))
	float HighlightLightRadius = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight")
	FLinearColor HighlightLightColor = FLinearColor(0.2f, 1.0f, 0.35f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight")
	float HighlightLightExtraZOffset = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight")
	TObjectPtr<UMaterialInterface> PrimarySelectedOverlayMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight")
	TObjectPtr<UMaterialInterface> HoveredOverlayMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight")
	bool bEnableDebugDrawHighlight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight", meta = (ClampMin = "10.0"))
	float DebugHighlightRadius = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Highlight", meta = (ClampMin = "0.5"))
	float DebugHighlightThickness = 3.0f;

	// ---- State accessors ----

	UFUNCTION(BlueprintCallable, Category = "Selection")
	void SetPrimarySelected(bool bSelected);

	UFUNCTION(BlueprintCallable, Category = "Selection")
	void SetSecondarySelected(bool bSelected);

	UFUNCTION(BlueprintCallable, Category = "Selection")
	void SetHovered(bool bHovered);

	UFUNCTION(BlueprintPure, Category = "Selection")
	bool IsPrimarySelected() const { return bIsPrimarySelected; }

	UFUNCTION(BlueprintPure, Category = "Selection")
	bool IsSecondarySelected() const { return bIsSecondarySelected; }

	UFUNCTION(BlueprintPure, Category = "Selection")
	bool IsHovered() const { return bIsHovered; }

	// Fired when primary or secondary state changes
	UPROPERTY(BlueprintAssignable, Category = "Selection")
	FOnSelectionStateChanged OnSelectionStateChanged;

private:
	UPROPERTY()
	bool bIsPrimarySelected = false;

	UPROPERTY()
	bool bIsSecondarySelected = false;

	UPROPERTY()
	bool bIsHovered = false;

	UPROPERTY()
	FVector OriginalActorScale = FVector::OneVector;

	UPROPERTY()
	bool bHasCapturedOriginalScale = false;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> SelectionLabelComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> SelectionFxComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> SelectionPointLightComponent = nullptr;

	void BroadcastIfChanged();
	void ApplyVisualState();
	void EnsureSelectionLabel();
	void UpdateSelectionLabelVisibility();
};
