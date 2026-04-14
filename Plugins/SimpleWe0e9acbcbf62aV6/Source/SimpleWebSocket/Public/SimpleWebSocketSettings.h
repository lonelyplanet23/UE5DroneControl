// Copyright (c) 2025 mengzhishanghun. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimpleWebSocketSettings.generated.h"

USTRUCT(BlueprintType)
struct FWebSocketConfig
{
	GENERATED_BODY()

	/** WebSocket 连接地址 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, config, Category="WebSocket")
	FString Url = TEXT("ws://127.0.0.1:8000");

	/** 启动时是否自动连接 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, config, Category="WebSocket")
	bool bAutoConnect = false;
};

UCLASS(config=SimpleWebSocket, defaultconfig, meta=(DisplayName="Simple WebSocket Settings"))
class SIMPLEWEBSOCKET_API USimpleWebSocketSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, config, Category="WebSocket")
	TMap<FName, FWebSocketConfig> WebSocketConnections;
};
