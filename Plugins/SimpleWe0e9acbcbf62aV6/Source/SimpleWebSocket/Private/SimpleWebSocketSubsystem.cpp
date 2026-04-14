// Copyright (c) 2025 mengzhishanghun. All rights reserved.

#include "SimpleWebSocketSubsystem.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "SimpleWebSocketLog.h"

void USimpleWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogSimpleWebSocket, Log, TEXT("SimpleWebSocketSubsystem Initialized"));
    Instance = this;
    const USimpleWebSocketSettings* Settings = GetDefault<USimpleWebSocketSettings>();
    for (auto const& It : Settings->WebSocketConnections)
    {
        if (It.Value.bAutoConnect)
        {
            ConnectWebSocket(It.Key);
        }
    }
}

void USimpleWebSocketSubsystem::Deinitialize()
{
    Super::Deinitialize();
    CloseAllWebSocket();
    Instance = nullptr;
}

bool USimpleWebSocketSubsystem::GetWebSocketConfig(const FName Name, FWebSocketConfig& Config)
{
    if (!Instance)return false;
    const USimpleWebSocketSettings* Settings = GetDefault<USimpleWebSocketSettings>();
    if (!Settings)return false;
    if (!Settings->WebSocketConnections.Contains(Name))return false;
    Config = Settings->WebSocketConnections.FindRef(Name);
    return true;
}

void USimpleWebSocketSubsystem::ConnectWebSocket(const FName Name)
{
    if (!Instance)return;
    const USimpleWebSocketSettings* Settings = GetDefault<USimpleWebSocketSettings>();
    const FWebSocketConfig* Config = Settings->WebSocketConnections.Find(Name);
    if (!Config)
    {
        UE_LOG(LogSimpleWebSocket, Warning, TEXT("[SimpleWebSocket] No config found for connection: %s"), *Name.ToString());
        return;
    }

    const TSharedPtr<IWebSocket> Socket = FWebSocketsModule::Get().CreateWebSocket(Config->Url);

    // 消息事件触发所有绑定的蓝图回调
    Socket->OnMessage().AddLambda([Name](const FString& Message) {
        if (!Instance)return;
        if (TArray<FSimpleWebSocketMessage>* HandlerList = Instance->MessageHandlers.Find(Name))
        {
            for (auto& Callback : *HandlerList)
            {
                Callback.ExecuteIfBound(Message);
            }
        }
    });

    Socket->OnConnected().AddLambda([Name]() {
        UE_LOG(LogSimpleWebSocket, Log, TEXT("[SimpleWebSocket] Connected: %s"), *Name.ToString());
    });

    Socket->OnConnectionError().AddLambda([Name](const FString& Error) {
        UE_LOG(LogSimpleWebSocket, Error, TEXT("[SimpleWebSocket] Connection error (%s): %s"), *Name.ToString(), *Error);
    });

    Socket->OnClosed().AddLambda([Name](int32 StatusCode, const FString& Reason, bool bWasClean) {
        UE_LOG(LogSimpleWebSocket, Warning, TEXT("[SimpleWebSocket] Closed (%s): %s"), *Name.ToString(), *Reason);
    });

    Socket->Connect();
    Instance->ActiveConnections.Add(Name, Socket);
}

bool USimpleWebSocketSubsystem::CheckWebSocketConnect(const FName Name)
{
    if (!Instance)return false;
    if (!Instance->ActiveConnections.Contains(Name))return false;
    return Instance->ActiveConnections[Name]->IsConnected();
}


void USimpleWebSocketSubsystem::SendWebSocketMessage(const FName Name, const FString& Msg)
{
    if (!Instance)return;
    if (const TSharedPtr<IWebSocket>* SocketPtr = Instance->ActiveConnections.Find(Name))
    {
        if (SocketPtr->IsValid() && (*SocketPtr)->IsConnected())
        {
            (*SocketPtr)->Send(Msg);
        }
        else
        {
            UE_LOG(LogSimpleWebSocket, Warning, TEXT("[SimpleWebSocket] Connection %s is not valid or not connected"), *Name.ToString());
        }
    }
    else
    {
        UE_LOG(LogSimpleWebSocket, Warning, TEXT("[SimpleWebSocket] Connection %s not found"), *Name.ToString());
    }
}

void USimpleWebSocketSubsystem::CloseWebSocket(const FName Name)
{
    if (!Instance)return;
    if (const TSharedPtr<IWebSocket>* SocketPtr = Instance->ActiveConnections.Find(Name))
    {
        if (SocketPtr->IsValid())
        {
            (*SocketPtr)->Close();
        }
        Instance->ActiveConnections.Remove(Name);
        UE_LOG(LogSimpleWebSocket, Log, TEXT("[SimpleWebSocket] Closed and removed connection: %s"), *Name.ToString());
    }
    else
    {
        UE_LOG(LogSimpleWebSocket, Warning, TEXT("[SimpleWebSocket] No active connection named: %s"), *Name.ToString());
    }
}

void USimpleWebSocketSubsystem::CloseAllWebSocket()
{
    if (!Instance)return;
    for (auto const& It : Instance->ActiveConnections)
    {
        It.Value->Close();
    }
    Instance->ActiveConnections.Empty();
    UE_LOG(LogSimpleWebSocket, Log, TEXT("[SimpleWebSocket] Closed and removed all connections"));
}

void USimpleWebSocketSubsystem::BindWebSocketMessage(const FName ConnectionName, const FSimpleWebSocketMessage& Callback)
{
    if (!Instance)return;
    if (!Callback.IsBound()) return;

    TArray<FSimpleWebSocketMessage>& HandlerList = Instance->MessageHandlers.FindOrAdd(ConnectionName);

    // 避免重复添加（只比较对象和函数指针）
    const bool bExists = HandlerList.ContainsByPredicate([&](const FSimpleWebSocketMessage& D) {
        return D.GetUObject() == Callback.GetUObject() &&
               D.GetFunctionName() == Callback.GetFunctionName();
    });

    if (!bExists)
    {
        HandlerList.Add(Callback);
    }
}

void USimpleWebSocketSubsystem::UnbindWebSocketMessage(const FName ConnectionName, const FSimpleWebSocketMessage& Callback)
{
    if (!Instance)return;
    if (TArray<FSimpleWebSocketMessage>* HandlerList = Instance->MessageHandlers.Find(ConnectionName))
    {
        HandlerList->RemoveAll([&](const FSimpleWebSocketMessage& D) {
            return D.GetUObject() == Callback.GetUObject() &&
                   D.GetFunctionName() == Callback.GetFunctionName();
        });
    }
}