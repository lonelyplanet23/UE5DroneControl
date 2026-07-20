// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOps/Drone/DroneGroundProjectionComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ConfigCacheIni.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UDroneGroundProjectionComponent::UDroneGroundProjectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDroneGroundProjectionComponent::BeginPlay()
{
	Super::BeginPlay();
	bCachedEnabled = IsGroundProjectionEnabled();
	TrailPoints.Empty();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 创建程序化网格组件挂载到 Owner
	CurtainMesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("CurtainMesh"));
	if (CurtainMesh)
	{
		CurtainMesh->SetupAttachment(Owner->GetRootComponent());
		// 幕帘网格使用世界坐标，不跟随 Owner 移动
		CurtainMesh->SetAbsolute(true, true, true);
		CurtainMesh->SetWorldLocation(FVector::ZeroVector);
		CurtainMesh->SetWorldRotation(FRotator::ZeroRotator);
		CurtainMesh->SetWorldScale3D(FVector::OneVector);
		CurtainMesh->RegisterComponent();

		// 关闭碰撞和阴影
		CurtainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CurtainMesh->SetCastShadow(false);

		CreateCurtainMaterial();
	}
}

void UDroneGroundProjectionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (CurtainMesh)
	{
		CurtainMesh->DestroyComponent();
		CurtainMesh = nullptr;
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UDroneGroundProjectionComponent::CreateCurtainMaterial()
{
	if (!CurtainMesh)
	{
		return;
	}

	// 优先加载项目中的半透明顶点色材质 M_CurtainBase
	UMaterialInterface* MatSource = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/DroneOps/Materials/M_CurtainBase.M_CurtainBase"));

	if (!MatSource)
	{
		// Fallback：使用引擎内置的半透明材质
		MatSource = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
		UE_LOG(LogTemp, Warning,
			TEXT("[GroundProjection] M_CurtainBase not found, using DefaultMaterial fallback. "
			     "Please create M_CurtainBase (Translucent + Unlit + VertexColor) for proper rendering."));
	}

	if (MatSource)
	{
		CurtainMaterial = UMaterialInstanceDynamic::Create(MatSource, this);
	}
}

void UDroneGroundProjectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 定期刷新全局开关缓存
	if (++FrameCounter >= CacheRefreshInterval)
	{
		FrameCounter = 0;
		bCachedEnabled = IsGroundProjectionEnabled();
	}

	// 关闭时隐藏并跳过
	if (!bCachedEnabled)
	{
		if (CurtainMesh && CurtainMesh->IsVisible())
		{
			CurtainMesh->SetVisibility(false);
		}
		return;
	}

	if (CurtainMesh && !CurtainMesh->IsVisible())
	{
		CurtainMesh->SetVisibility(true);
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();

	// 清除超时点
	const int32 OldCount = TrailPoints.Num();
	PruneExpiredPoints(CurrentTime);
	if (TrailPoints.Num() != OldCount)
	{
		bMeshDirty = true;
	}

	// 射线检测地面，记录新的轨迹点
	const FVector DronePos = Owner->GetActorLocation();
	FVector GroundHit;
	if (TraceToGround(DronePos, GroundHit))
	{
		bool bShouldRecord = true;
		if (TrailPoints.Num() > 0)
		{
			const float DistSq = FVector::DistSquared(TrailPoints.Last().AirPosition, DronePos);
			bShouldRecord = DistSq >= FMath::Square(MinRecordDistance);
		}

		if (bShouldRecord)
		{
			FTrailPoint NewPoint;
			NewPoint.AirPosition = DronePos;
			NewPoint.GroundPosition = GroundHit;
			NewPoint.Timestamp = CurrentTime;
			TrailPoints.Add(NewPoint);
			bMeshDirty = true;
		}
	}

	// 重建网格（有变化时，或每帧更新淡出效果）
	// 为了淡出效果平滑，每帧都重建顶点色
	RebuildCurtainMesh(CurrentTime);
}

bool UDroneGroundProjectionComponent::TraceToGround(const FVector& Start, FVector& OutHitPoint) const
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}

	const FVector End = Start + FVector(0.0f, 0.0f, -MaxTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);

	FHitResult HitResult;
	const bool bHit = World->LineTraceSingleByChannel(
		HitResult, Start, End, ECC_Visibility, QueryParams);

	if (bHit)
	{
		OutHitPoint = HitResult.ImpactPoint;
	}

	return bHit;
}

void UDroneGroundProjectionComponent::PruneExpiredPoints(float CurrentTime)
{
	int32 RemoveCount = 0;
	for (int32 i = 0; i < TrailPoints.Num(); ++i)
	{
		if (CurrentTime - TrailPoints[i].Timestamp > TrailLifetime)
		{
			RemoveCount = i + 1;
		}
		else
		{
			break;
		}
	}

	if (RemoveCount > 0)
	{
		TrailPoints.RemoveAt(0, RemoveCount);
	}
}

void UDroneGroundProjectionComponent::RebuildCurtainMesh(float CurrentTime)
{
	if (!CurtainMesh)
	{
		return;
	}

	// 点数不足，无法构成面
	if (TrailPoints.Num() < 2)
	{
		CurtainMesh->ClearMeshSection(0);
		return;
	}

	const int32 SegmentCount = TrailPoints.Num() - 1;
	const int32 VertexCount = TrailPoints.Num() * 2; // 每个点产生上下两个顶点
	const int32 TriangleCount = SegmentCount * 2;     // 每段两个三角形

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> VertexColors;

	Vertices.Reserve(VertexCount);
	Normals.Reserve(VertexCount);
	UV0.Reserve(VertexCount);
	VertexColors.Reserve(VertexCount);
	Triangles.Reserve(TriangleCount * 3);

	// ---- 生成顶点 ----
	for (int32 i = 0; i < TrailPoints.Num(); ++i)
	{
		const FTrailPoint& Pt = TrailPoints[i];

		// 计算淡出 Alpha
		const float Age = CurrentTime - Pt.Timestamp;
		const float RemainingLife = TrailLifetime - Age;
		float Alpha = CurtainColor.A; // 基础透明度
		if (RemainingLife <= FadeOutDuration)
		{
			Alpha *= FMath::Clamp(RemainingLife / FadeOutDuration, 0.0f, 1.0f);
		}

		const FLinearColor VertColor(CurtainColor.R, CurtainColor.G, CurtainColor.B, Alpha);

		// 上顶点（无人机位置）
		Vertices.Add(Pt.AirPosition);
		VertexColors.Add(VertColor);
		UV0.Add(FVector2D(static_cast<float>(i) / SegmentCount, 0.0f));

		// 下顶点（地面投影位置）
		Vertices.Add(Pt.GroundPosition);
		VertexColors.Add(VertColor);
		UV0.Add(FVector2D(static_cast<float>(i) / SegmentCount, 1.0f));
	}

	// ---- 生成三角形索引 ----
	// 每段由 4 个顶点构成一个四边形，拆成 2 个三角形
	// 顶点排列：i*2=上A, i*2+1=下A, (i+1)*2=上B, (i+1)*2+1=下B
	//
	//   上A ---- 上B
	//    |  \     |
	//    |   \    |
	//    |    \   |
	//   下A ---- 下B
	//
	// 正面三角形（从外侧看逆时针 → 法线朝外）
	// 双面渲染：正反各加一组

	for (int32 i = 0; i < SegmentCount; ++i)
	{
		const int32 TopA = i * 2;
		const int32 BotA = i * 2 + 1;
		const int32 TopB = (i + 1) * 2;
		const int32 BotB = (i + 1) * 2 + 1;

		// 正面
		Triangles.Add(TopA);
		Triangles.Add(BotA);
		Triangles.Add(TopB);

		Triangles.Add(TopB);
		Triangles.Add(BotA);
		Triangles.Add(BotB);

		// 背面（翻转绕序）
		Triangles.Add(TopA);
		Triangles.Add(TopB);
		Triangles.Add(BotA);

		Triangles.Add(TopB);
		Triangles.Add(BotB);
		Triangles.Add(BotA);
	}

	// 法线：简单地用水平法线（具体方向不重要，Unlit 材质不依赖法线）
	Normals.SetNum(VertexCount);
	for (int32 i = 0; i < VertexCount; ++i)
	{
		Normals[i] = FVector(0.0f, 1.0f, 0.0f);
	}

	// 更新网格
	CurtainMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, VertexColors, TArray<FProcMeshTangent>(), false);

	// 应用材质
	if (CurtainMaterial)
	{
		CurtainMesh->SetMaterial(0, CurtainMaterial);
	}
}

void UDroneGroundProjectionComponent::ClearTrail()
{
	TrailPoints.Empty();
	if (CurtainMesh)
	{
		CurtainMesh->ClearMeshSection(0);
	}
}

bool UDroneGroundProjectionComponent::IsGroundProjectionEnabled()
{
	bool bEnabled = true;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("DroneDisplay"), TEXT("bShowGroundProjectionRay"), bEnabled, GGameIni);
	}
	return bEnabled;
}
