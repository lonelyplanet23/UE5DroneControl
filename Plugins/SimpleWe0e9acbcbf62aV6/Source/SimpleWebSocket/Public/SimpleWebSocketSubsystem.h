// Copyright (c) 2025 mengzhishanghun. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "SimpleWebSocketSettings.h"
#include "SimpleWebSocketSubsystem.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FSimpleWebSocketMessage, const FString&, Message);

UCLASS()
class SIMPLEWEBSOCKET_API USimpleWebSocketSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static bool GetWebSocketConfig(const FName Name, FWebSocketConfig& Config);

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static void ConnectWebSocket(const FName Name);

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static bool CheckWebSocketConnect(const FName Name);

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static void SendWebSocketMessage(const FName Name, const FString& Msg);

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static void CloseWebSocket(const FName Name);

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static void CloseAllWebSocket();

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static void BindWebSocketMessage(const FName ConnectionName, const FSimpleWebSocketMessage& Callback);

	UFUNCTION(BlueprintCallable, Category="WebSocket")
	static void UnbindWebSocketMessage(const FName ConnectionName, const FSimpleWebSocketMessage& Callback);
private:
	TMap<FName, TSharedPtr<IWebSocket>> ActiveConnections;
	TMap<FName, TArray<FSimpleWebSocketMessage>> MessageHandlers;
	static inline USimpleWebSocketSubsystem* Instance;
};
