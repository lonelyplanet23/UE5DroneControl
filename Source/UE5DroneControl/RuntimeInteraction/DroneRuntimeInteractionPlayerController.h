#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "../PathEditor/DroneWaypointActor.h"
#include "DroneRuntimeInteractionPlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaypointSelectedSignature, ADroneWaypointActor*, SelectedWaypoint);

UCLASS()
class UE5DRONECONTROL_API ADroneRuntimeInteractionPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADroneRuntimeInteractionPlayerController();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint Editing")
	bool bIsEditMode = true;


	UPROPERTY(BlueprintAssignable, Category = "Waypoint Editing")
	FOnWaypointSelectedSignature OnWaypointSelected;

	UFUNCTION(BlueprintCallable, Category = "Waypoint Editing")
	void SetEditMode(bool bEnableEditMode);

	UFUNCTION(BlueprintCallable, Category = "Waypoint Editing")
	void ToggleEditMode();

	UFUNCTION(BlueprintCallable, Category = "Waypoint Editing")
	ADroneWaypointActor* GetSelectedWaypoint() const;

	UFUNCTION(BlueprintCallable, Category = "Waypoint Editing")
	void SetSelectedWaypoint(ADroneWaypointActor* NewWaypoint);

	/** B 键：返回主菜单关卡 */
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void ReturnToMainMenu();

	/** 主菜单关卡名称，B 键跳转目标 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Navigation")
	FName MainMenuLevelName = FName("MainMenu");

	/** 源锚点无人机 ID：该路径的第一个节点被锁定，不允许拖拽修改位置。编辑关卡固定为 1。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Waypoint Editing")
	int32 AnchorSourceDroneId = 1;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	void ConfigureRuntimeMouseInteraction();
	void HandleToggleEditMode();
	void HandleLeftMousePressed();
	void HandleLeftMouseReleased();
	void HandleReturnToMainMenu();
	void SetWaypointDragging(bool bEnableDragging);
	void UpdateDraggedWaypointLocation();
	float ResolveAxisDragDelta();
	bool CanInteractWithWaypoint(const ADroneWaypointActor* WaypointActor) const;
	void ClearSelectedWaypoint();
	void SetActiveAxis(EGizmoAxis NewActiveAxis);

	UPROPERTY()
	TObjectPtr<ADroneWaypointActor> SelectedWaypoint = nullptr;

	EGizmoAxis ActiveAxis = EGizmoAxis::None;

	bool bIsDraggingWaypoint = false;

	FVector2D LastMouseScreenPosition = FVector2D::ZeroVector;

	float GizmoDragSensitivity = 1.0f;
};
