#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h" // �̳�������ǻ���
#include "Networking.h"
#include "Sockets.h"
#include "RealTimeDroneReceiver.generated.h"

/**
 * ��ʵ�ɻ����ն� (ARealTimeDroneReceiver)
 * �������߳���ѯ (Polling) ģʽ��������߳�ί����ĳЩ�����²����������⡣
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

	// ƽ���ƶ��ٶ�
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float SmoothSpeed = 5.0f;

	// �������� (1.0 = ������)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float ScaleFactor = 1.0f;

	// �Զ�ת��
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bAutoFaceTarget = true;

private:
	// ���� Socket
	FSocket* ListenSocket;

	// Ŀ��λ�û���
	FVector TargetLocation;

	// �������ݰ�����
	void ProcessPacket(const TArray<uint8>& Data);
};