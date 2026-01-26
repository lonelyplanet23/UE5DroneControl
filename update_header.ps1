# 读取头文件
$headerPath = "Source\UE5DroneControl\RealTimeDroneReceiver.h"
$content = Get-Content $headerPath -Raw -Encoding UTF8

# 在 bUseReceivedRotation 后添加 MaxUpdateFrequency
$pattern = '(\tbool bUseReceivedRotation = true;)'
$replacement = @'
$1

	// 【新增】数据处理频率限制（Hz），0表示不限制，推荐60
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float MaxUpdateFrequency = 60.0f;
'@

$content = $content -replace $pattern, $replacement

# 在 bReceivedDataInAutoDetect 后添加新的成员变量
$pattern2 = '(\tbool bReceivedDataInAutoDetect = false;)'
$replacement2 = @'
$1

	// 【新增】上次处理数据的时间
	float LastUpdateTime = 0.0f;

	// 【新增】待处理的数据队列（存储最新的一条）
	TArray<uint8> PendingData;
	bool bHasPendingData = false;
'@

$content = $content -replace $pattern2, $replacement2

# 保存文件
Set-Content $headerPath $content -Encoding UTF8 -NoNewline

Write-Host "✓ 头文件更新成功！"
