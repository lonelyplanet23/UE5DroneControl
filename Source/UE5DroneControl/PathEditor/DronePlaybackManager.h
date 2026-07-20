#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DronePathSaveLibrary.h"
#include "DronePlaybackManager.generated.h"

class ADronePathActor;
class AMultiDroneCharacter;

USTRUCT()
struct FDronePlaybackPauseState
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<ADronePathActor> PathActor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AActor> DroneActor = nullptr;

	UPROPERTY(Transient)
	int32 SegmentIndex = INDEX_NONE;

	UPROPERTY(Transient)
	float RemainingSegmentTime = 0.0f;
};

UCLASS()
class UE5DRONECONTROL_API ADronePlaybackManager : public AActor
{
	GENERATED_BODY()

public:
	ADronePlaybackManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Playback")
	TSubclassOf<AActor> DroneActorClass;

	/** When true, PlayFromData drives existing AMultiDroneCharacter shadow drones instead of spawning preview actors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Playback")
	bool bUseExistingShadowDrones = false;

	UPROPERTY(Transient)
	TArray<ADronePathActor*> ActivePaths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Playback")
	bool bIsPlaying = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Playback")
	bool bIsPaused = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Playback")
	bool bLoop = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Playback")
	float PlaybackTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Playback")
	float StartTime = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	void PlayFromData(const FDronePathsSaveData& Data);

	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	void PausePlayback();

	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	void ResumePlayback();

	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	void StopPlayback();

	/**
	 * Spawn AMultiDroneCharacter instances so that PlayFromData (bUseExistingShadowDrones=true)
	 * can find them. Existing ones are reused; extras are destroyed. All spawned ones are
	 * tracked in SpawnedMultiDrones and destroyed on StopPlayback.
	 */
	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	void EnsurePlaybackDrones(int32 Count);

	/**
	 * 全局停止：停掉 World 内所有 ADronePlaybackManager 的播放，并停止且销毁
	 * 所有仍存在的 ADronePathActor（含卡在循环中的路径及其可视化）。
	 * 作为统一的全局停止入口，供派发面板全局停止按钮与 JSON 播放面板共用。
	 */
	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	static void StopAndClearAllInWorld(UWorld* World);

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> ActiveDrones;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedPlaybackDrones;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedMultiDrones;

	UPROPERTY(Transient)
	TArray<FDronePlaybackPauseState> PausedPathStates;

	UPROPERTY(Transient)
	FDronePathsSaveData CachedPlaybackData;

	void ClearActivePlaybackActors();
	AActor* SpawnPlaybackDrone(const FVector& SpawnLocation);
	TArray<AMultiDroneCharacter*> FindShadowDrones() const;
	FDronePathsSaveData RemapDataToAnchor(const FDronePathsSaveData& Data, const FVector& NewAnchorLocation) const;
};
