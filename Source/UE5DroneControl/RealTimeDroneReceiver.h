#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h" // �̳�������ǻ���
#include "Networking.h"
#include "Sockets.h"
#include "RealTimeDroneReceiver.generated.h"

/**
 * YAML ��ʽ�����˻����ݽṹ
 * ֧�ֽ��������˻����յ� YAML ��ʽ����
 */
USTRUCT(BlueprintType)
struct FDroneYAMLData
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Timestamp = 0;

	// λ�� (NED����ϵ����λ����)
	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	// ��Ԫ�� (X, Y, Z, W)
	UPROPERTY()
	FQuat Quaternion = FQuat::Identity;

	// �ٶ� (��ѡ�����ڵ���)
	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	// ���ٶ� (��ѡ)
	UPROPERTY()
	FVector AngularVelocity = FVector::ZeroVector;
};

/**
 * ��ʵ�ɻ����ն� (ARealTimeDroneReceiver)
 * �������߳���ѯ (Polling) ģʽ��֧�� YAML ��ʽ�����˻�����
 * NED����ϵת��ΪUE5����ϵ����תΪ����
 */
UCLASS()
class UE5DRONECONTROL_API ARealTimeDroneReceiver : public AUE5DroneControlCharacter
{
	GENERATED_BODY()

public:
	// ���캯��
	ARealTimeDroneReceiver();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ��д Tick ÿһ֡�����������
	virtual void Tick(float DeltaTime) override;

	// --- ���������� ---

	// �����˿�
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	int32 ListenPort = 8888;

	// ���������Ƿ������Զ��˿ڼ��
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bAutoDetectPort = false;

	// ���������Զ����Ķ˿ڷ�Χ - ��ʼ
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config", meta = (EditCondition = "bAutoDetectPort"))
	int32 PortScanStart = 7000;

	// ���������Զ����Ķ˿ڷ�Χ - ����
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config", meta = (EditCondition = "bAutoDetectPort"))
	int32 PortScanEnd = 9000;

	// ���������Զ���ⳬʱʱ�䣨�룩
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config", meta = (EditCondition = "bAutoDetectPort"))
	float AutoDetectTimeout = 10.0f;

	// ƽ���ƶ��ٶ�
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float SmoothSpeed = 5.0f;

	// �������� (Ĭ��1.0����Ϊ��λת������ProcessPacket�д���)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float ScaleFactor = 1.0f;

	// �Զ�ת��
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bAutoFaceTarget = true;

	// �Ƿ�ʹ���յ���ŷ���ǽ�����ת
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bUseReceivedRotation = true;

	// 銆愭柊澧炪€戞暟鎹鐞嗛鐜囬檺鍒讹紙Hz锛夛紝0琛ㄧず涓嶉檺鍒讹紝鎺ㄨ崘60
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float MaxUpdateFrequency = 60.0f;

	// 【新增】旋转死区阈值（度），小于此值的旋转变化将被忽略，用于消除抖动
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float RotationDeadZone = 0.5f;

private:
	// ���� Socket
	FSocket* ListenSocket;

	// ��ʼλ�ã���Ϊ����ԭ�㣩
	FVector InitialLocation = FVector::ZeroVector;

	// Ŀ��λ�û���
	FVector TargetLocation;

	// Ŀ����ת����
	FRotator TargetRotation;

	// 【新增】上一次的旋转（用于死区过滤）
	FRotator LastRotation;

	// 【关键修复】参考位置（第一次接收到的NED位置，用于计算相对偏移）
	FVector ReferencePosition = FVector::ZeroVector;

	// 【关键修复】是否已接收第一次数据
	bool bHasReceivedFirstData = false;

	// ���������Զ����ʱʹ�õ���ʱ�˿�
	int32 CurrentDetectedPort = -1;

	// ���������Զ���⿪ʼʱ��
	float AutoDetectStartTime = 0.0f;

	// ���������Զ�������յ����ݵı�־
	bool bReceivedDataInAutoDetect = false;

	// 銆愭柊澧炪€戜笂娆″鐞嗘暟鎹殑鏃堕棿
	float LastUpdateTime = 0.0f;

	// 銆愭柊澧炪€戝緟澶勭悊鐨勬暟鎹槦鍒楋紙瀛樺偍鏈€鏂扮殑涓€鏉★級
	TArray<uint8> PendingData;
	bool bHasPendingData = false;

	// ���� YAML ��ʽ���ݰ�
	void ProcessPacket(const TArray<uint8>& Data);

	// 銆愭柊澧炪€戜粎鏇存柊濮挎€佹暟鎹紙鐢ㄤ簬棰戠巼闄愬埗鏃讹級
	void UpdateRotationOnly(const TArray<uint8>& Data);

	// YAML ��������
	bool ParseYAMLData(const FString& YAMLString, FDroneYAMLData& OutData);

	// ��Ԫ��תŷ���� (ZYX˳��)
	FRotator QuatToEuler(const FQuat& Q);

	// NED ����ϵת UE5 ����ϵ (��ת����)
	FVector NEDToUE5(const FVector& NEDPos);

	// ���������Զ����˿� - ɨ��˿ڷ�Χ�ҵ������ݵĶ˿�
	void AutoDetectPort();

	// ���������������� Socket ��ָ���˿�
	bool CreateAndBindSocket(int32 Port);
};