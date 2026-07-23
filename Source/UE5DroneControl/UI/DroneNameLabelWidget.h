// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneNameLabelWidget.generated.h"

class UTextBlock;

/**
 * Screen-space name label displayed above each drone (mirror and shadow).
 * Placed on a UWidgetComponent set to Screen space so it never rotates
 * with the camera.  The owning actor calls ApplyLabelSettings() whenever
 * the name, colour, or font size changes.
 */
UCLASS()
class UE5DRONECONTROL_API UDroneNameLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Main text block – built at runtime if the Blueprint doesn't bind one. */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LabelText = nullptr;

	/** Apply a new display name. */
	UFUNCTION(BlueprintCallable, Category = "DroneLabel")
	void SetLabelName(const FString& Name);

	/** Apply a new tint colour. */
	UFUNCTION(BlueprintCallable, Category = "DroneLabel")
	void SetLabelColor(FLinearColor Color);

	/** Apply a new font size (only affects this label, not the sidebar panel). */
	UFUNCTION(BlueprintCallable, Category = "DroneLabel")
	void SetLabelFontSize(int32 Size);

	/** Convenience: set all three at once. */
	UFUNCTION(BlueprintCallable, Category = "DroneLabel")
	void ApplyLabelSettings(const FString& Name, FLinearColor Color, int32 FontSize);

protected:
	virtual void NativeOnInitialized() override;

private:
	void EnsureLabelText();
};
