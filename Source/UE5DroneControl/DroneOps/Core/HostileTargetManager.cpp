// Copyright Epic Games, Inc. All Rights Reserved.

#include "HostileTargetManager.h"
#include "HostileTargetActor.h"
#include "MultiDroneCharacter.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UHostileTargetManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RefreshRegistry();
	UE_LOG(LogTemp, Log, TEXT("[HostileTargetManager] Initialized"));
}

void UHostileTargetManager::Deinitialize()
{
	RegisteredTargets.Empty();
	AssignedMap.Empty();
	CachedRegistry = nullptr;
	Super::Deinitialize();
}

void UHostileTargetManager::RefreshRegistry()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			CachedRegistry = GI->GetSubsystem<UDroneRegistrySubsystem>();
		}
	}
}

void UHostileTargetManager::RegisterTarget(AHostileTargetActor* Target)
{
	if (!IsValid(Target))
	{
		return;
	}

	FScopeLock Lock(&TargetsLock);

	if (RegisteredTargets.Contains(Target))
	{
		return;
	}

	RegisteredTargets.Add(Target);
	UE_LOG(LogTemp, Log, TEXT("[HostileTargetManager] Registered target T-%d"), Target->TargetId);
}

void UHostileTargetManager::UnregisterTarget(AHostileTargetActor* Target)
{
	if (!IsValid(Target))
	{
		return;
	}

	FScopeLock Lock(&TargetsLock);
	RegisteredTargets.Remove(Target);
	AssignedMap.Remove(Target->TargetId);
	UE_LOG(LogTemp, Log, TEXT("[HostileTargetManager] Unregistered target T-%d"), Target->TargetId);
}

void UHostileTargetManager::UnregisterTargetById(int32 TargetId)
{
	FScopeLock Lock(&TargetsLock);
	for (int32 i = RegisteredTargets.Num() - 1; i >= 0; --i)
	{
		if (IsValid(RegisteredTargets[i]) && RegisteredTargets[i]->TargetId == TargetId)
		{
			RegisteredTargets.RemoveAt(i);
			break;
		}
	}
	AssignedMap.Remove(TargetId);
}

AHostileTargetActor* UHostileTargetManager::GetTarget(int32 TargetId) const
{
	FScopeLock Lock(&TargetsLock);
	for (TObjectPtr<AHostileTargetActor> Target : RegisteredTargets)
	{
		if (IsValid(Target) && Target->TargetId == TargetId)
		{
			return Target.Get();
		}
	}
	return nullptr;
}

TArray<AHostileTargetActor*> UHostileTargetManager::GetAllTargets() const
{
	FScopeLock Lock(&TargetsLock);
	TArray<AHostileTargetActor*> Result;
	for (TObjectPtr<AHostileTargetActor> Target : RegisteredTargets)
	{
		if (IsValid(Target))
		{
			Result.Add(Target.Get());
		}
	}
	return Result;
}

TArray<AHostileTargetActor*> UHostileTargetManager::GetUndiscoveredTargets() const
{
	FScopeLock Lock(&TargetsLock);
	TArray<AHostileTargetActor*> Result;
	for (TObjectPtr<AHostileTargetActor> Target : RegisteredTargets)
	{
		if (IsValid(Target) && !Target->bIsDiscovered)
		{
			Result.Add(Target.Get());
		}
	}
	return Result;
}

TArray<AHostileTargetActor*> UHostileTargetManager::GetDiscoveredTargets() const
{
	FScopeLock Lock(&TargetsLock);
	TArray<AHostileTargetActor*> Result;
	for (TObjectPtr<AHostileTargetActor> Target : RegisteredTargets)
	{
		if (IsValid(Target) && Target->bIsDiscovered)
		{
			Result.Add(Target.Get());
		}
	}
	return Result;
}

bool UHostileTargetManager::CheckDroneDetection(int32 DroneId, const FVector& DroneLocation,
	AHostileTargetActor*& OutTarget, float& OutDistance) const
{
	OutTarget = nullptr;
	OutDistance = 0.0f;

	FScopeLock Lock(&TargetsLock);

	for (TObjectPtr<AHostileTargetActor> Target : RegisteredTargets)
	{
		if (!IsValid(Target) || Target->bIsDiscovered)
		{
			continue;
		}

		const FVector TargetLocation = Target->GetHostileTargetLocation();
		const float Dist = FVector::Dist(DroneLocation, TargetLocation);

		if (Dist <= Target->DiscoveryRadius)
		{
			OutTarget = Target.Get();
			OutDistance = Dist;
			return true;
		}
	}

	return false;
}

TArray<TPair<int32, int32>> UHostileTargetManager::CheckAllPatrollingDrones()
{
	TArray<TPair<int32, int32>> Results;

	RefreshRegistry();
	if (!CachedRegistry)
	{
		return Results;
	}

	const TArray<int32> PatrollingDrones = GetPatrollingDrones();
	if (PatrollingDrones.IsEmpty())
	{
		return Results;
	}

	TArray<AHostileTargetActor*> Undiscovered = GetUndiscoveredTargets();
	if (Undiscovered.IsEmpty())
	{
		return Results;
	}

	TSet<int32> ReservedDroneIds;
	// One target produces one candidate: choose the nearest eligible patrol drone,
	// with DroneId as the deterministic tie breaker. Do not let iteration order assign it.
	for (AHostileTargetActor* Target : Undiscovered)
	{
		if (!IsValid(Target) || Target->bIsDiscovered)
		{
			continue;
		}

		int32 BestDroneId = 0;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 DroneId : PatrollingDrones)
		{
			if (ReservedDroneIds.Contains(DroneId))
			{
				continue;
			}
			AMultiDroneCharacter* Shadow = GetShadowDrone(DroneId);
			if (!IsValid(Shadow))
			{
				continue;
			}

			const float Dist = FVector::Dist(Shadow->GetActorLocation(), Target->GetHostileTargetLocation());
			if (Dist <= Target->DiscoveryRadius &&
				(Dist < BestDistance || (FMath::IsNearlyEqual(Dist, BestDistance) &&
					(BestDroneId == 0 || DroneId < BestDroneId))))
			{
				BestDroneId = DroneId;
				BestDistance = Dist;
			}
		}

		if (BestDroneId > 0)
		{
			Results.Add(TPair<int32, int32>(BestDroneId, Target->TargetId));
			ReservedDroneIds.Add(BestDroneId);
		}
	}

	return Results;
}

//重置离开检测范围的目标
void UHostileTargetManager::ResetUndetectedTargets()
{
	FScopeLock Lock(&TargetsLock);

	for (TObjectPtr<AHostileTargetActor> Target : RegisteredTargets)
	{
		if (!IsValid(Target) || !Target->bIsDiscovered)
		{
			continue;
		}

		const int32* Assigned = AssignedMap.Find(Target->TargetId);
		const int32 AssignedDroneId = Assigned ? *Assigned : 0;
		if (AssignedDroneId <= 0)
		{
			continue;
		}

		AMultiDroneCharacter* Shadow = GetShadowDrone(AssignedDroneId);
		if (!IsValid(Shadow))
		{
			continue;
		}

		const float Dist = FVector::Dist(Shadow->GetActorLocation(), Target->GetHostileTargetLocation());
		if (Dist > Target->DiscoveryRadius * 1.2f)
		{
			if (CachedRegistry)
			{
				CachedRegistry->UpdateLocalState(AssignedDroneId, EUELocalDroneState::None);
			}
			Target->ResetTarget();
			AssignedMap.Remove(Target->TargetId);
			UE_LOG(LogTemp, Log, TEXT("[HostileTargetManager] Target T-%d left detection range, reset"),
				Target->TargetId);
		}
	}
}

bool UHostileTargetManager::AssignDroneToTarget(int32 TargetId, int32& OutDroneId, float& OutDistance) const
{
	OutDroneId = 0;
	OutDistance = 0.0f;

	AHostileTargetActor* Target = GetTarget(TargetId);
	if (!IsValid(Target) || Target->bIsDiscovered)
	{
		return false;
	}

	if (!CachedRegistry)
	{
		return false;
	}

	const TArray<int32> PatrollingDrones = GetPatrollingDrones();
	if (PatrollingDrones.IsEmpty())
	{
		return false;
	}

	int32 BestDroneId = 0;
	float BestDistance = FLT_MAX;
	const FVector TargetLocation = Target->GetHostileTargetLocation();

	for (int32 DroneId : PatrollingDrones)
	{
		AMultiDroneCharacter* Shadow = GetShadowDrone(DroneId);
		if (!IsValid(Shadow))
		{
			continue;
		}

		const float Dist = FVector::Dist(Shadow->GetActorLocation(), TargetLocation);
		if (Dist > Target->DiscoveryRadius)
		{
			continue;
		}

		if (Dist < BestDistance || (FMath::IsNearlyEqual(Dist, BestDistance) && DroneId < BestDroneId))
		{
			BestDroneId = DroneId;
			BestDistance = Dist;
		}
	}

	if (BestDroneId > 0)
	{
		OutDroneId = BestDroneId;
		OutDistance = BestDistance;
		return true;
	}

	return false;
}

bool UHostileTargetManager::TryAssignTarget(int32 TargetId, int32 DroneId)
{
	FScopeLock Lock(&TargetsLock);

	if (AssignedMap.Contains(TargetId))
	{
		return false;
	}

	AHostileTargetActor* Target = GetTarget(TargetId);
	if (!IsValid(Target) || Target->bIsDiscovered)
	{
		return false;
	}

	AssignedMap.Add(TargetId, DroneId);
	Target->MarkDiscovered(DroneId);

	UE_LOG(LogTemp, Log, TEXT("[HostileTargetManager] Target T-%d assigned to drone %d"),
		TargetId, DroneId);

	return true;
}

void UHostileTargetManager::UnassignTarget(int32 TargetId)
{
	FScopeLock Lock(&TargetsLock);

	if (AssignedMap.Contains(TargetId))
	{
		AssignedMap.Remove(TargetId);
		UE_LOG(LogTemp, Log, TEXT("[HostileTargetManager] Target T-%d unassigned"), TargetId);
	}
}

bool UHostileTargetManager::IsTargetAssigned(int32 TargetId) const
{
	FScopeLock Lock(&TargetsLock);
	return AssignedMap.Contains(TargetId);
}

int32 UHostileTargetManager::GetAssignedDroneId(int32 TargetId) const
{
	FScopeLock Lock(&TargetsLock);
	const int32* Found = AssignedMap.Find(TargetId);
	return Found ? *Found : 0;
}

TArray<int32> UHostileTargetManager::GetPatrollingDrones() const
{
	TArray<int32> Result;

	if (!CachedRegistry)
	{
		return Result;
	}

	FDroneTelemetrySnapshot Snap;
	for (const FDroneDescriptor& Desc : CachedRegistry->GetAllDroneDescriptors())
	{
		if (!CachedRegistry->GetTelemetry(Desc.DroneId, Snap))
		{
			continue;
		}

		const bool bBusyInLocalPreview = Snap.LocalState == EUELocalDroneState::TargetDetectedPending ||
			Snap.LocalState == EUELocalDroneState::LocalAttacking ||
			Snap.LocalState == EUELocalDroneState::LocalAttackCompleted;
		if (Snap.Availability == EDroneAvailability::Online &&
			Snap.TaskMode == EDroneCommandMode::Patrol &&
			Snap.TaskState == EDroneTaskState::Patrolling && !bBusyInLocalPreview)
		{
			Result.Add(Desc.DroneId);
		}
	}

	return Result;
}

AMultiDroneCharacter* UHostileTargetManager::GetShadowDrone(int32 DroneId) const
{
	if (!CachedRegistry)
	{
		return nullptr;
	}

	APawn* Pawn = CachedRegistry->GetSenderPawn(DroneId);
	if (!Pawn)
	{
		return nullptr;
	}

	return Cast<AMultiDroneCharacter>(Pawn);
}
