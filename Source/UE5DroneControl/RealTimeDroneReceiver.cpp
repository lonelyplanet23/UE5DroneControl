#include "RealTimeDroneReceiver.h"

// --- 引入必要的底层头文件 ---
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Common/UdpSocketBuilder.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/CapsuleComponent.h" 

ARealTimeDroneReceiver::ARealTimeDroneReceiver()
{
	PrimaryActorTick.bCanEverTick = true;
	TargetLocation = FVector::ZeroVector;
	TargetRotation = FRotator::ZeroRotator;
	ListenSocket = nullptr;

	// === 【关键】禁用重力和物理模拟 ===
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// 禁用重力 - 这是防止下落的关键！
		Movement->GravityScale = 0.0f;
		Movement->bUseFlatBaseForFloorChecks = true;
		// 禁用物理中的垂直速度
		Movement->Velocity.Z = 0.0f;
	}

	// === 【修改】智能碰撞设置 ===
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// 1. 确保碰撞是开启的 (Profile = Pawn)，这样它才能检测到地板
		Capsule->SetCollisionProfileName(TEXT("Pawn"));

		// 2. 但是！我们要忽略其他"Pawn"（也就是玩家和其他飞机）
		// 这样你们两个飞机重叠时才不会互相挤飞
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

		// 3. 忽略摄像机，防止遮挡视线
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		
		// 4. 禁用物理模拟 - 防止碰撞弹起
		Capsule->SetSimulatePhysics(false);
	}
}

void ARealTimeDroneReceiver::BeginPlay()
{
	Super::BeginPlay();

	// 【关键】记录初始位置作为坐标原点
	InitialLocation = GetActorLocation();
	TargetLocation = InitialLocation;
	TargetRotation = GetActorRotation();

	// 【再次确保】禁用重力和物理
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GravityScale = 0.0f;
		Movement->SetMovementMode(MOVE_Flying);  // 切换到飞行模式，完全禁用重力
	}

	// === 1. 如果启用自动检测，则开始扫描端口 ===
	if (bAutoDetectPort)
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [RealTimeDrone] 启动自动端口检测，范围: %d - %d <<<"), PortScanStart, PortScanEnd);
		AutoDetectPort();
		return;
	}

	// === 1. 绑定到固定端口 ===
	CreateAndBindSocket(ListenPort);
}

void ARealTimeDroneReceiver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (ListenSocket)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}
}

void ARealTimeDroneReceiver::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// === 【新增】频率限制逻辑 ===
	float CurrentTime = GetWorld()->GetTimeSeconds();
	float TimeSinceLastUpdate = CurrentTime - LastUpdateTime;
	float MinUpdateInterval = (MaxUpdateFrequency > 0.0f) ? (1.0f / MaxUpdateFrequency) : 0.0f;

	// === 2. 主动轮询数据 ===
	if (ListenSocket)
	{
		uint32 Size;
		while (ListenSocket->HasPendingData(Size))
		{
			TArray<uint8> ReceivedData;
			ReceivedData.SetNumUninitialized(FMath::Min(Size, 65507u));

			int32 Read = 0;
			TSharedRef<FInternetAddr> SenderAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

			if (ListenSocket->RecvFrom(ReceivedData.GetData(), ReceivedData.Num(), Read, *SenderAddr))
			{
				if (Read > 0)
				{
					// 【诊断】每100个包打印一次接收到的原始UDP数据
					static int32 RecvCounter = 0;
					RecvCounter++;

					if (RecvCounter % 100 == 0)
					{
						FDateTime Now = FDateTime::Now();
						FString TimeStr = FString::Printf(TEXT("%02d:%02d:%02d.%03d"),
							Now.GetHour(), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond());

						FString RawData = FString::FromBlob(ReceivedData.GetData(), FMath::Min(Read, 400));
						UE_LOG(LogTemp, Warning, TEXT(">>> [%s] [接收#%d] 来自: %s, 大小: %d 字节"),
							*TimeStr, RecvCounter, *SenderAddr->ToString(true), Read);
						UE_LOG(LogTemp, Warning, TEXT("[UDP原始数据]\n%s"), *RawData);
					}

					// 【新增】自动检测时标记收到数据
					if (bAutoDetectPort && CurrentDetectedPort >= 0)
					{
						bReceivedDataInAutoDetect = true;
						UE_LOG(LogTemp, Warning, TEXT(">>> [AutoDetect] 在端口 %d 收到数据! 检测完成! <<<"), CurrentDetectedPort);
					}

					// 【修复】频率限制：位置限频，但姿态始终更新
					if (MaxUpdateFrequency > 0.0f && TimeSinceLastUpdate < MinUpdateInterval)
					{
						// 保存最新数据到待处理队列（覆盖旧数据）
						PendingData = ReceivedData;
						bHasPendingData = true;

						// 【关键修复】立即更新姿态数据，避免旋转回弹
						// 解析YAML但只更新姿态，不更新位置
						UpdateRotationOnly(ReceivedData);
					}
					else
					{
						// 直接处理数据（位置+姿态）
						ProcessPacket(ReceivedData);
						LastUpdateTime = CurrentTime;
						bHasPendingData = false;
					}
				}
			}
		}

		// 【新增】处理待处理的数据（按频率限制）
		if (bHasPendingData && TimeSinceLastUpdate >= MinUpdateInterval)
		{
			ProcessPacket(PendingData);
			LastUpdateTime = CurrentTime;
			bHasPendingData = false;
		}
	}

	// === 2.5 自动端口检测逻辑 ===
	if (bAutoDetectPort && CurrentDetectedPort >= 0 && !bReceivedDataInAutoDetect)
	{
		float ElapsedTime = GetWorld()->GetTimeSeconds() - AutoDetectStartTime;

		// 每 0.5 秒切换一次端口
		if (ElapsedTime > 0.5f && CurrentDetectedPort < PortScanEnd)
		{
			CurrentDetectedPort++;
			AutoDetectStartTime = GetWorld()->GetTimeSeconds();
			bReceivedDataInAutoDetect = false;

			if (!CreateAndBindSocket(CurrentDetectedPort))
			{
				CurrentDetectedPort++;
			}

			UE_LOG(LogTemp, Warning, TEXT(">>> [AutoDetect] 切换到端口 %d"), CurrentDetectedPort);
		}
		else if (ElapsedTime > AutoDetectTimeout || CurrentDetectedPort >= PortScanEnd)
		{
			// 超时或扫描完毕，使用最后尝试的端口
			bAutoDetectPort = false;
			UE_LOG(LogTemp, Error, TEXT(">>> [AutoDetect] 端口检测超时! 使用端口 %d"), CurrentDetectedPort);
		}
	}

	// === 3. 平滑移动 ===
	FVector CurrentLoc = GetActorLocation();
	FVector NewLoc = FMath::VInterpTo(CurrentLoc, TargetLocation, DeltaTime, SmoothSpeed);

	// 启用 Sweep 后，飞机移动时会进行碰撞检测
	SetActorLocation(NewLoc, true);

	// === 4. 平滑旋转 ===
	// 【优先级1】如果启用了"使用接收的旋转"，则使用无人机发送的姿态
	if (bUseReceivedRotation)
	{
		FRotator CurrentRot = GetActorRotation();
		FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRotation, DeltaTime, 10.0f);
		SetActorRotation(NewRot);
	}
	// 【优先级2】否则，如果启用了"自动朝向"，则朝向移动方向
	else if (bAutoFaceTarget)
	{
		FVector Direction = (NewLoc - CurrentLoc);
		if (Direction.SizeSquared() > 1.0f)
		{
			FRotator TargetRot = UKismetMathLibrary::MakeRotFromX(Direction);
			FRotator NewRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 10.0f);
			SetActorRotation(FRotator(0, NewRot.Yaw, 0));  // 只使用 Yaw，保持水平
		}
	}
	// 【优先级3】如果两个都关闭，则保持当前旋转不变
}

bool ARealTimeDroneReceiver::ParseYAMLData(const FString& YAMLString, FDroneYAMLData& OutData)
{
	// 分行处理 YAML 数据
	TArray<FString> Lines;
	YAMLString.ParseIntoArray(Lines, TEXT("\n"), true);

	FString TrimmedLine;
	TArray<float> PositionData;
	TArray<float> QuatData;
	TArray<float> VelocityData;
	TArray<float> AngularVelocityData;

	for (const FString& Line : Lines)
	{
		TrimmedLine = Line.TrimStart().TrimEnd();

		// 解析 timestamp
		if (TrimmedLine.StartsWith(TEXT("timestamp:")))
		{
			FString ValueStr = TrimmedLine.RightChop(10).TrimStart();
			OutData.Timestamp = FCString::Atoi64(*ValueStr);
		}

		// 解析 position (数组格式)
		if (TrimmedLine.StartsWith(TEXT("- ")) && PositionData.Num() < 3)
		{
			FString ValueStr = TrimmedLine.RightChop(2);
			PositionData.Add(FCString::Atof(*ValueStr));
		}

		// 解析 q (四元数，数组格式)
		if (TrimmedLine.Equals(TEXT("q:")) || (TrimmedLine.StartsWith(TEXT("- ")) && QuatData.Num() < 4 && PositionData.Num() == 3))
		{
			if (TrimmedLine.StartsWith(TEXT("- ")))
			{
				FString ValueStr = TrimmedLine.RightChop(2);
				QuatData.Add(FCString::Atof(*ValueStr));
			}
		}

		// 解析 velocity
		if (TrimmedLine.Equals(TEXT("velocity:")) || (TrimmedLine.StartsWith(TEXT("- ")) && VelocityData.Num() < 3 && PositionData.Num() == 3 && QuatData.Num() == 4))
		{
			if (TrimmedLine.StartsWith(TEXT("- ")))
			{
				FString ValueStr = TrimmedLine.RightChop(2);
				VelocityData.Add(FCString::Atof(*ValueStr));
			}
		}

		// 解析 angular_velocity
		if (TrimmedLine.Equals(TEXT("angular_velocity:")) || (TrimmedLine.StartsWith(TEXT("- ")) && AngularVelocityData.Num() < 3 && PositionData.Num() == 3 && QuatData.Num() == 4 && VelocityData.Num() == 3))
		{
			if (TrimmedLine.StartsWith(TEXT("- ")))
			{
				FString ValueStr = TrimmedLine.RightChop(2);
				AngularVelocityData.Add(FCString::Atof(*ValueStr));
			}
		}
	}

	// 验证必要数据是否存在
	if (PositionData.Num() != 3 || QuatData.Num() != 4)
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [YAML Parse] 解析失败: Position数据=%d, Quat数据=%d"), PositionData.Num(), QuatData.Num());
		return false;
	}

	// 填充解析结果
	OutData.Position = FVector(PositionData[0], PositionData[1], PositionData[2]);
	OutData.Quaternion = FQuat(QuatData[0], QuatData[1], QuatData[2], QuatData[3]);

	if (VelocityData.Num() == 3)
	{
		OutData.Velocity = FVector(VelocityData[0], VelocityData[1], VelocityData[2]);
	}

	if (AngularVelocityData.Num() == 3)
	{
		OutData.AngularVelocity = FVector(AngularVelocityData[0], AngularVelocityData[1], AngularVelocityData[2]);
	}

	return true;
}

FRotator ARealTimeDroneReceiver::QuatToEuler(const FQuat& Q)
{
	// ==================== NED 四元数 → UE5 欧拉角转换 ====================
	//
	// 无人机四元数（NED 坐标系）：
	//   表示从 NED 参考系到机体系的旋转
	//   q = [qx, qy, qz, qw]
	//
	// UE5 欧拉角（FRotator）：
	//   Pitch (俯仰，绕 Y 轴)
	//   Yaw   (偏航，绕 Z 轴)
	//   Roll  (翻滚，绕 X 轴)
	//
	// 注意：NED 和 UE5 坐标系不同，需要转换
	// ====================================================================

	// 方法1：使用 UE5 内置的四元数转换（推荐）
	// 先将 NED 四元数转换为 UE5 四元数
	FQuat UE5Quat = Q;

	// 【关键】NED 到 UE5 的四元数转换
	// NED: X=North, Y=East, Z=Down
	// UE5: X=Forward, Y=Right, Z=Up
	// 需要绕 X 轴旋转 180° 来翻转 Z 轴（Down → Up）
	FQuat ConversionQuat = FQuat(FVector(1, 0, 0), PI);  // 绕 X 轴旋转 180°
	UE5Quat = ConversionQuat * Q;

	// 转换为欧拉角
	FRotator EulerAngles = UE5Quat.Rotator();

	return EulerAngles;
}

FVector ARealTimeDroneReceiver::NEDToUE5(const FVector& NEDPos)
{
	// ==================== NED 坐标系 → UE5 坐标系转换 ====================
	//
	// 无人机坐标系（NED - North East Down）：
	//   X = North (北，米)
	//   Y = East  (东，米)
	//   Z = Down  (下，米，向下为正)
	//
	// UE5 坐标系（左手系）：
	//   X = Forward (前，厘米)
	//   Y = Right   (右，厘米)
	//   Z = Up      (上，厘米，向上为正)
	//
	// 转换规则：
	//   1. NED_X (North) → UE5_X (Forward) - 假设地图的 +X 指向北方
	//   2. NED_Y (East)  → UE5_Y (Right)
	//   3. NED_Z (Down)  → UE5_Z (Up)，取负值（Down变Up）
	//   4. 单位转换：米 × 100 = 厘米
	//
	// 注意：如果你的 UE5 地图的 +X 轴不是指向北方，需要额外旋转
	// ====================================================================

	FVector UE5Pos(
		NEDPos.X * 100.0f,      // North → X (米 → 厘米)
		NEDPos.Y * 100.0f,      // East  → Y (米 → 厘米)
		-NEDPos.Z * 100.0f      // -Down → Up (米 → 厘米，取负)
	);

	// 应用用户自定义缩放因子（默认 1.0）
	UE5Pos *= ScaleFactor;

	return UE5Pos;
}

void ARealTimeDroneReceiver::UpdateRotationOnly(const TArray<uint8>& Data)
{
	// 【优化】仅更新姿态，不更新位置（用于频率限制时避免旋转回弹）
	// 为了节省内存，我们直接解析四元数，不创建完整的DroneData

	FString YAMLString;
	YAMLString.AppendChars((const ANSICHAR*)Data.GetData(), Data.Num());

	// 【优化】简化解析，只提取四元数部分
	TArray<FString> Lines;
	YAMLString.ParseIntoArray(Lines, TEXT("\n"), true);

	TArray<float> QuatData;
	bool bFoundQSection = false;

	for (const FString& Line : Lines)
	{
		FString TrimmedLine = Line.TrimStart().TrimEnd();

		// 找到 q: 标记
		if (TrimmedLine.Equals(TEXT("q:")))
		{
			bFoundQSection = true;
			continue;
		}

		// 在 q: 之后收集4个浮点数
		if (bFoundQSection && TrimmedLine.StartsWith(TEXT("- ")) && QuatData.Num() < 4)
		{
			FString ValueStr = TrimmedLine.RightChop(2);
			QuatData.Add(FCString::Atof(*ValueStr));
		}

		// 收集完4个数就退出
		if (QuatData.Num() >= 4)
		{
			break;
		}
	}

	// 验证是否成功解析
	if (QuatData.Num() == 4)
	{
		FQuat Q(QuatData[0], QuatData[1], QuatData[2], QuatData[3]);
		TargetRotation = QuatToEuler(Q);
	}
}

void ARealTimeDroneReceiver::ProcessPacket(const TArray<uint8>& Data)
{
	// 将字节数据转换为字符串
	FString YAMLString;
	YAMLString.AppendChars((const ANSICHAR*)Data.GetData(), Data.Num());

	// 【优化】减少日志输出，只在需要时打印
	#if !UE_BUILD_SHIPPING
	static int32 PacketCounter = 0;
	PacketCounter++;
	// 每100个包才打印一次
	if (PacketCounter % 100 == 0)
	{
		UE_LOG(LogTemp, Log, TEXT(">>> [ProcessPacket] 已处理 %d 个数据包"), PacketCounter);
	}
	#endif

	// 解析 YAML 数据
	FDroneYAMLData DroneData;
	if (!ParseYAMLData(YAMLString, DroneData))
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [ProcessPacket] YAML 解析失败"));
		return;
	}

	// 【诊断】打印原始NED数据（每50个包打印一次）
	#if !UE_BUILD_SHIPPING
	if (PacketCounter % 50 == 0)
	{
		// 获取当前时间
		FDateTime Now = FDateTime::Now();
		FString TimeStr = FString::Printf(TEXT("%02d:%02d:%02d.%03d"),
			Now.GetHour(), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond());

		UE_LOG(LogTemp, Warning, TEXT(">>> [%s] [原始数据] NED Position: (%.6f, %.6f, %.6f) 米"),
			*TimeStr, DroneData.Position.X, DroneData.Position.Y, DroneData.Position.Z);
	}
	#endif

	// 【关键修复】第一次接收数据时，记录参考位置
	if (!bHasReceivedFirstData)
	{
		ReferencePosition = DroneData.Position;
		bHasReceivedFirstData = true;

		// 获取当前时间
		FDateTime Now = FDateTime::Now();
		FString TimeStr = FString::Printf(TEXT("%02d:%02d:%02d.%03d"),
			Now.GetHour(), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond());

		UE_LOG(LogTemp, Warning, TEXT(">>> [%s] [参考位置] 已记录参考位置: (%.6f, %.6f, %.6f) 米"),
			*TimeStr, ReferencePosition.X, ReferencePosition.Y, ReferencePosition.Z);
	}

	// 【关键修复】计算相对偏移量（当前位置 - 参考位置）
	FVector RelativeOffset = DroneData.Position - ReferencePosition;

	// 【诊断】打印相对偏移量
	#if !UE_BUILD_SHIPPING
	if (PacketCounter % 50 == 0)
	{
		// 获取当前时间
		FDateTime Now = FDateTime::Now();
		FString TimeStr = FString::Printf(TEXT("%02d:%02d:%02d.%03d"),
			Now.GetHour(), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond());

		UE_LOG(LogTemp, Warning, TEXT(">>> [%s] [相对偏移] Relative Offset: (%.6f, %.6f, %.6f) 米"),
			*TimeStr, RelativeOffset.X, RelativeOffset.Y, RelativeOffset.Z);
	}
	#endif

	// 坐标系和单位转换: NED (米) -> UE5 (厘米)
	FVector NEDOffset = NEDToUE5(RelativeOffset);

	// 【诊断】打印转换后的偏移量
	#if !UE_BUILD_SHIPPING
	if (PacketCounter % 50 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [转换后] UE5 Offset: (%.2f, %.2f, %.2f) 厘米"),
			NEDOffset.X, NEDOffset.Y, NEDOffset.Z);
	}
	#endif

	// 【关键修改】以初始位置为原点，NEDOffset 是相对位移
	FVector NewTarget = InitialLocation + NEDOffset;

	// 【诊断】打印最终目标位置和初始位置
	#if !UE_BUILD_SHIPPING
	if (PacketCounter % 50 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [最终位置] Target: (%.2f, %.2f, %.2f) | Initial: (%.2f, %.2f, %.2f)"),
			NewTarget.X, NewTarget.Y, NewTarget.Z,
			InitialLocation.X, InitialLocation.Y, InitialLocation.Z);
		UE_LOG(LogTemp, Warning, TEXT(">>> [距离差] 从Initial到Target的距离: %.2f 厘米"),
			FVector::Dist(InitialLocation, NewTarget));
	}
	#endif

	// 四元数转欧拉角
	FRotator NewRotation = QuatToEuler(DroneData.Quaternion);

	// 【关键】更新目标位置和旋转
	TargetLocation = NewTarget;
	TargetRotation = NewRotation;

	// 【诊断】屏幕调试信息 - 显示完整数据流
	#if !UE_BUILD_SHIPPING
	if (GEngine && PacketCounter % 10 == 0)
	{
		FVector CurrentPos = GetActorLocation();
		FString DebugMsg = FString::Printf(
			TEXT("NED原始:(%.3f,%.3f,%.3f)m | UE5目标:(%.0f,%.0f,%.0f)cm | 当前:(%.0f,%.0f,%.0f)cm | 距离差:%.0fcm"),
			DroneData.Position.X, DroneData.Position.Y, DroneData.Position.Z,
			NewTarget.X, NewTarget.Y, NewTarget.Z,
			CurrentPos.X, CurrentPos.Y, CurrentPos.Z,
			FVector::Dist(CurrentPos, NewTarget)
		);
		GEngine->AddOnScreenDebugMessage(123, 0.1f, FColor::Yellow, DebugMsg);
	}
	#endif
}

bool ARealTimeDroneReceiver::CreateAndBindSocket(int32 Port)
{
	// 【新增函数】创建并绑定 Socket 到指定端口
	if (ListenSocket)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}

	// 绑定到 0.0.0.0 (监听所有网卡)
	FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);

	ListenSocket = FUdpSocketBuilder(TEXT("RealTimePollingSocket"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(2 * 1024 * 1024);

	if (ListenSocket)
	{
		UE_LOG(LogTemp, Warning, TEXT(">>> [RealTimeDrone] 监听启动! Port: %d <<<"), Port);
		ListenPort = Port;  // 更新当前监听端口
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT(">>> [RealTimeDrone] 错误: 端口 %d 绑定失败! <<<"), Port);
		return false;
	}
}

void ARealTimeDroneReceiver::AutoDetectPort()
{
	// 【新增函数】自动扫描端口范围找到有数据的端口
	AutoDetectStartTime = GetWorld()->GetTimeSeconds();
	CurrentDetectedPort = PortScanStart;
	bReceivedDataInAutoDetect = false;

	// 尝试绑定第一个端口
	if (!CreateAndBindSocket(CurrentDetectedPort))
	{
		CurrentDetectedPort++;
	}

	UE_LOG(LogTemp, Warning, TEXT(">>> [AutoDetect] 开始扫描端口 %d, 等待数据..."), CurrentDetectedPort);
}