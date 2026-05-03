#include "DronePathConflictLibrary.h"

#include "DronePathActor.h"
#include "Math/Box.h"

namespace DronePathConflict
{
	constexpr float WaypointRadiusCm = 50.0f;
	constexpr float ConflictDistanceCm = WaypointRadiusCm * 2.0f;
	constexpr float IntersectionToleranceCm = 1.0f;
	constexpr float ParallelThreshold = KINDA_SMALL_NUMBER;

	struct FPathSegment
	{
		TWeakObjectPtr<ADronePathActor> PathActor;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FBox Bounds = FBox(EForceInit::ForceInit);
		int32 StartWaypointIndex = INDEX_NONE;
		int32 EndWaypointIndex = INDEX_NONE;
	};

	struct FSegmentConflictResult
	{
		bool bHasConflict = false;
		bool bIsIntersection = false;
		float ClosestDistanceCm = TNumericLimits<float>::Max();
		FVector ClosestPointA = FVector::ZeroVector;
		FVector ClosestPointB = FVector::ZeroVector;
		FVector VisualLocation = FVector::ZeroVector;
	};

	static FString GetPathDisplayName(const ADronePathActor* PathActor)
	{
		if (!IsValid(PathActor))
		{
			return TEXT("InvalidPath");
		}

		return PathActor->PathId.IsNone() ? PathActor->GetName() : PathActor->PathId.ToString();
	}

	static FVector ClosestPointOnSegment(const FVector& Point, const FVector& SegmentStart, const FVector& SegmentEnd)
	{
		const FVector Segment = SegmentEnd - SegmentStart;
		const float SegmentLengthSquared = Segment.SizeSquared();

		if (SegmentLengthSquared <= KINDA_SMALL_NUMBER)
		{
			return SegmentStart;
		}

		const float Projection = FVector::DotProduct(Point - SegmentStart, Segment) / SegmentLengthSquared;
		return SegmentStart + (FMath::Clamp(Projection, 0.0f, 1.0f) * Segment);
	}

	static void ComputeClosestPointsOnSegments(
		const FVector& SegmentStartA,
		const FVector& SegmentEndA,
		const FVector& SegmentStartB,
		const FVector& SegmentEndB,
		FVector& OutClosestPointA,
		FVector& OutClosestPointB)
	{
		const FVector SegmentA = SegmentEndA - SegmentStartA;
		const FVector SegmentB = SegmentEndB - SegmentStartB;
		const FVector DeltaStart = SegmentStartA - SegmentStartB;

		const float LengthSquaredA = SegmentA.SizeSquared();
		const float LengthSquaredB = SegmentB.SizeSquared();

		if (LengthSquaredA <= KINDA_SMALL_NUMBER && LengthSquaredB <= KINDA_SMALL_NUMBER)
		{
			OutClosestPointA = SegmentStartA;
			OutClosestPointB = SegmentStartB;
			return;
		}

		if (LengthSquaredA <= KINDA_SMALL_NUMBER)
		{
			OutClosestPointA = SegmentStartA;
			OutClosestPointB = ClosestPointOnSegment(SegmentStartA, SegmentStartB, SegmentEndB);
			return;
		}

		if (LengthSquaredB <= KINDA_SMALL_NUMBER)
		{
			OutClosestPointB = SegmentStartB;
			OutClosestPointA = ClosestPointOnSegment(SegmentStartB, SegmentStartA, SegmentEndA);
			return;
		}

		const float DotAA = LengthSquaredA;
		const float DotAB = FVector::DotProduct(SegmentA, SegmentB);
		const float DotBB = LengthSquaredB;
		const float DotAC = FVector::DotProduct(SegmentA, DeltaStart);
		const float DotBC = FVector::DotProduct(SegmentB, DeltaStart);

		const float Denominator = (DotAA * DotBB) - (DotAB * DotAB);

		float ParameterA = 0.0f;
		float ParameterB = 0.0f;

		if (Denominator > ParallelThreshold)
		{
			ParameterA = FMath::Clamp(((DotAB * DotBC) - (DotBB * DotAC)) / Denominator, 0.0f, 1.0f);
		}

		ParameterB = (DotAB * ParameterA + DotBC) / DotBB;

		if (ParameterB < 0.0f)
		{
			ParameterB = 0.0f;
			ParameterA = FMath::Clamp(-DotAC / DotAA, 0.0f, 1.0f);
		}
		else if (ParameterB > 1.0f)
		{
			ParameterB = 1.0f;
			ParameterA = FMath::Clamp((DotAB - DotAC) / DotAA, 0.0f, 1.0f);
		}

		OutClosestPointA = SegmentStartA + (ParameterA * SegmentA);
		OutClosestPointB = SegmentStartB + (ParameterB * SegmentB);
	}

	static FSegmentConflictResult AnalyzeSegmentConflict(const FPathSegment& SegmentA, const FPathSegment& SegmentB)
	{
		FSegmentConflictResult Result;

		if (!SegmentA.Bounds.Intersect(SegmentB.Bounds))
		{
			return Result;
		}

		ComputeClosestPointsOnSegments(
			SegmentA.Start,
			SegmentA.End,
			SegmentB.Start,
			SegmentB.End,
			Result.ClosestPointA,
			Result.ClosestPointB);

		Result.ClosestDistanceCm = FVector::Dist(Result.ClosestPointA, Result.ClosestPointB);
		if (Result.ClosestDistanceCm >= ConflictDistanceCm)
		{
			return Result;
		}

		Result.bHasConflict = true;
		Result.bIsIntersection = Result.ClosestDistanceCm <= IntersectionToleranceCm;
		Result.VisualLocation = (Result.ClosestPointA + Result.ClosestPointB) * 0.5f;
		return Result;
	}

	static void BuildSegmentsForPath(ADronePathActor* PathActor, TArray<FPathSegment>& OutSegments)
	{
		if (!IsValid(PathActor) || !PathActor->bParticipatesInConflictChecks)
		{
			return;
		}

		const int32 WaypointCount = PathActor->GetWaypointCount();
		if (WaypointCount <= 0)
		{
			return;
		}

		auto AddSegment = [&](const int32 StartIndex, const int32 EndIndex)
		{
			FPathSegment Segment;
			Segment.PathActor = PathActor;
			Segment.StartWaypointIndex = StartIndex;
			Segment.EndWaypointIndex = EndIndex;
			Segment.Start = PathActor->GetWaypointWorldLocation(StartIndex);
			Segment.End = PathActor->GetWaypointWorldLocation(EndIndex);
			Segment.Bounds += Segment.Start;
			Segment.Bounds += Segment.End;
			Segment.Bounds = Segment.Bounds.ExpandBy(ConflictDistanceCm);
			OutSegments.Add(MoveTemp(Segment));
		};

		if (WaypointCount == 1)
		{
			AddSegment(0, 0);
			return;
		}

		for (int32 WaypointIndex = 0; WaypointIndex < WaypointCount - 1; ++WaypointIndex)
		{
			AddSegment(WaypointIndex, WaypointIndex + 1);
		}

		if (PathActor->bClosedLoop && WaypointCount > 2)
		{
			AddSegment(WaypointCount - 1, 0);
		}
	}
}

TArray<FString> UDronePathConflictLibrary::CheckPathConflictsWithVolume(const TArray<ADronePathActor*>& PathActors)
{
	TArray<FString> ConflictMessages;

	TArray<TObjectPtr<ADronePathActor>> UniquePaths;
	UniquePaths.Reserve(PathActors.Num());

	for (ADronePathActor* PathActor : PathActors)
	{
		if (IsValid(PathActor) && PathActor->bParticipatesInConflictChecks && !UniquePaths.Contains(PathActor))
		{
			UniquePaths.Add(PathActor);
		}
	}

	if (UniquePaths.Num() < 2)
	{
		for (ADronePathActor* PathActor : UniquePaths)
		{
			if (IsValid(PathActor))
			{
				PathActor->ClearConflictVisualization();
			}
		}

		return ConflictMessages;
	}

	for (ADronePathActor* PathActor : UniquePaths)
	{
		if (IsValid(PathActor))
		{
			PathActor->ClearConflictVisualization();
		}
	}

	TArray<TArray<DronePathConflict::FPathSegment>> SegmentsByPath;
	SegmentsByPath.SetNum(UniquePaths.Num());

	for (int32 PathIndex = 0; PathIndex < UniquePaths.Num(); ++PathIndex)
	{
		DronePathConflict::BuildSegmentsForPath(UniquePaths[PathIndex], SegmentsByPath[PathIndex]);
	}

	for (int32 PathIndexA = 0; PathIndexA < UniquePaths.Num() - 1; ++PathIndexA)
	{
		const TArray<DronePathConflict::FPathSegment>& SegmentsA = SegmentsByPath[PathIndexA];
		if (SegmentsA.IsEmpty())
		{
			continue;
		}

		for (int32 PathIndexB = PathIndexA + 1; PathIndexB < UniquePaths.Num(); ++PathIndexB)
		{
			const TArray<DronePathConflict::FPathSegment>& SegmentsB = SegmentsByPath[PathIndexB];
			if (SegmentsB.IsEmpty())
			{
				continue;
			}

			for (const DronePathConflict::FPathSegment& SegmentA : SegmentsA)
			{
				for (const DronePathConflict::FPathSegment& SegmentB : SegmentsB)
				{
					const DronePathConflict::FSegmentConflictResult ConflictResult = DronePathConflict::AnalyzeSegmentConflict(SegmentA, SegmentB);
					if (!ConflictResult.bHasConflict)
					{
						continue;
					}
					if (ADronePathActor* ConflictedPathA = SegmentA.PathActor.Get())
					{
						ConflictedPathA->MarkConflictSegment(SegmentA.StartWaypointIndex, SegmentA.EndWaypointIndex);
					}

					if (ADronePathActor* ConflictedPathB = SegmentB.PathActor.Get())
					{
						ConflictedPathB->MarkConflictSegment(SegmentB.StartWaypointIndex, SegmentB.EndWaypointIndex);
					}

					if (ConflictResult.bIsIntersection)
					{
						ConflictMessages.Add(FString::Printf(
							TEXT("Path crossing: %s [%d -> %d] crosses %s [%d -> %d] at (X=%.2f, Y=%.2f, Z=%.2f), separation=%.2f cm."),
							*DronePathConflict::GetPathDisplayName(SegmentA.PathActor.Get()),
							SegmentA.StartWaypointIndex,
							SegmentA.EndWaypointIndex,
							*DronePathConflict::GetPathDisplayName(SegmentB.PathActor.Get()),
							SegmentB.StartWaypointIndex,
							SegmentB.EndWaypointIndex,
							ConflictResult.VisualLocation.X,
							ConflictResult.VisualLocation.Y,
							ConflictResult.VisualLocation.Z,
							ConflictResult.ClosestDistanceCm));
					}
					else
					{
						ConflictMessages.Add(FString::Printf(
							TEXT("Path conflict: %s [%d -> %d] near %s [%d -> %d] at (X=%.2f, Y=%.2f, Z=%.2f), separation=%.2f cm."),
							*DronePathConflict::GetPathDisplayName(SegmentA.PathActor.Get()),
							SegmentA.StartWaypointIndex,
							SegmentA.EndWaypointIndex,
							*DronePathConflict::GetPathDisplayName(SegmentB.PathActor.Get()),
							SegmentB.StartWaypointIndex,
							SegmentB.EndWaypointIndex,
							ConflictResult.VisualLocation.X,
							ConflictResult.VisualLocation.Y,
							ConflictResult.VisualLocation.Z,
							ConflictResult.ClosestDistanceCm));
					}
				}
			}
		}
	}

	for (ADronePathActor* PathActor : UniquePaths)
	{
		if (IsValid(PathActor))
		{
			PathActor->RefreshConflictVisualization();
		}
	}

	return ConflictMessages;
}
