// Copyright Epic Games, Inc. All Rights Reserved.

#include "HostileTargetActor.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HostileTargetManager.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"

int32 AHostileTargetActor::NextTargetId = 1;

AHostileTargetActor::AHostileTargetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 根组件
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	// 网格组件（红色标记）
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootScene);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);

	// 加载默认网格（Cube）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}
	MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 55.0f));
	MeshComponent->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.1f));
	MeshComponent->SetVisibility(true);
	MeshComponent->SetHiddenInGame(false);
	MeshComponent->SetRenderCustomDepth(true);

	MarkerLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MarkerLight"));
	MarkerLight->SetupAttachment(RootScene);
	MarkerLight->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));
	MarkerLight->SetLightColor(FLinearColor(1.0f, 0.08f, 0.03f));
	MarkerLight->SetIntensity(900.0f);
	MarkerLight->SetAttenuationRadius(500.0f);

	// 发现半径可视化（仅编辑器，运行时碰撞检测用）
	DiscoverySphere = CreateDefaultSubobject<USphereComponent>(TEXT("DiscoverySphere"));
	DiscoverySphere->SetupAttachment(RootScene);
	DiscoverySphere->SetSphereRadius(DiscoveryRadius);
	DiscoverySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DiscoverySphere->SetHiddenInGame(true);

	// 标签组件
	LabelComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelComponent"));
	LabelComponent->SetupAttachment(RootScene);
	LabelComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	LabelComponent->SetWorldSize(40.0f);
	LabelComponent->SetText(FText::FromString(TEXT("T-0")));
	LabelComponent->SetHorizontalAlignment(EHTA_Center);
	LabelComponent->SetVerticalAlignment(EVRTA_TextCenter);
	LabelComponent->SetVisibility(true);
	LabelComponent->SetHiddenInGame(false);
	LabelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AHostileTargetActor::BeginPlay()
{
	Super::BeginPlay();

	// 自动分配ID（如果未设置）
	if (TargetId <= 0)
	{
		TargetId = GenerateUniqueTargetId(GetWorld());
	}

	// 同步发现半径到可视化组件
	if (DiscoverySphere)
	{
		DiscoverySphere->SetSphereRadius(DiscoveryRadius);
	}

	UpdateLabel();

	// 向管理器注册
	if (UWorld* World = GetWorld())
	{
		if (UHostileTargetManager* Manager = World->GetSubsystem<UHostileTargetManager>())
		{
			Manager->RegisterTarget(this);
		}
	}

	UpdateVisuals();
}

void AHostileTargetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 从管理器注销
	if (UWorld* World = GetWorld())
	{
		if (UHostileTargetManager* Manager = World->GetSubsystem<UHostileTargetManager>())
		{
			Manager->UnregisterTarget(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AHostileTargetActor::PostLoad()
{
	Super::PostLoad();
	if (DiscoverySphere)
	{
		DiscoverySphere->SetSphereRadius(DiscoveryRadius);
	}
	UpdateLabel();
}

#if WITH_EDITOR
void AHostileTargetActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AHostileTargetActor, DiscoveryRadius))
	{
		if (DiscoverySphere)
		{
			DiscoverySphere->SetSphereRadius(DiscoveryRadius);
		}
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AHostileTargetActor, TargetId))
	{
		UpdateLabel();
	}
}
#endif

FVector AHostileTargetActor::GetHostileTargetLocation() const
{
	return GetActorLocation();
}

void AHostileTargetActor::MarkDiscovered(int32 DroneId)
{
	if (bIsDiscovered)
	{
		return;
	}

	bIsDiscovered = true;
	AssignedDroneId = DroneId;
	bIsAssigned = true;
	UpdateVisuals();

	UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Target %d discovered by drone %d"), TargetId, DroneId);
}

void AHostileTargetActor::ResetTarget()
{
	bIsDiscovered = false;
	AssignedDroneId = 0;
	bIsAssigned = false;
	UpdateVisuals();

	UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Target %d reset"), TargetId);
}

int32 AHostileTargetActor::GenerateUniqueTargetId(UWorld* World)
{
	// 检查已存在的目标点，避免ID冲突
	if (!World)
	{
		return NextTargetId++;
	}

	int32 MaxId = 0;
	for (TActorIterator<AHostileTargetActor> It(World); It; ++It)
	{
		if (AHostileTargetActor* Existing = *It)
		{
			if (Existing->TargetId > MaxId)
			{
				MaxId = Existing->TargetId;
			}
		}
	}

	NextTargetId = FMath::Max(NextTargetId, MaxId + 1);
	return NextTargetId++;
}

void AHostileTargetActor::UpdateVisuals()
{
	if (!MeshComponent)
	{
		return;
	}

	// 已发现：灰色；未发现：红色
	const FLinearColor Color = bIsDiscovered ? FLinearColor::Gray : FLinearColor::Red;
	MeshComponent->SetVectorParameterValueOnMaterials(TEXT("BaseColor"), FVector(Color.R, Color.G, Color.B));

	// 已发现时降低透明度
	if (UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(0)))
	{
		DynMat->SetScalarParameterValue(TEXT("Opacity"), bIsDiscovered ? 0.5f : 1.0f);
	}
}

void AHostileTargetActor::UpdateLabel()
{
	if (LabelComponent)
	{
		const FString LabelText = FString::Printf(TEXT("T-%d"), TargetId);
		LabelComponent->SetText(FText::FromString(LabelText));
	}
}
