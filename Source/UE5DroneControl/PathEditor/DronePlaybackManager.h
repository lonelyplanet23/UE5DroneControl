#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DronePathSaveLibrary.h"
#include "DronePlaybackManager.generated.h"

class ADronePathActor;

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

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> ActiveDrones;

	UPROPERTY(Transient)
	TArray<FDronePlaybackPauseState> PausedPathStates;

	UPROPERTY(Transient)
	FDronePathsSaveData CachedPlaybackData;

	void ClearActivePlaybackActors();
	AActor* SpawnPlaybackDrone(const FVector& SpawnLocation);
};
