# 在头文件中添加UpdateRotationOnly声明
$headerPath = "Source\UE5DroneControl\RealTimeDroneReceiver.h"
$content = Get-Content $headerPath -Raw -Encoding UTF8

# 在 ProcessPacket 后添加 UpdateRotationOnly
$pattern = '(\tvoid ProcessPacket\(const TArray<uint8>& Data\);)'
$replacement = @'
$1

	// 【新增】仅更新姿态数据（用于频率限制时）
	void UpdateRotationOnly(const TArray<uint8>& Data);
'@

$content = $content -replace $pattern, $replacement
Set-Content $headerPath $content -Encoding UTF8 -NoNewline

Write-Host "✓ 头文件已更新，添加了UpdateRotationOnly声明"
