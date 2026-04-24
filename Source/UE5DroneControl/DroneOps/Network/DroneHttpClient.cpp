#include "DroneOps/Network/DroneHttpClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

void UDroneHttpClient::Get(const FString& Path, FOnHttpResponse OnComplete)
{
	auto Req = MakeRequest(TEXT("GET"), Path, TEXT(""));
	Req->OnProcessRequestComplete().BindUObject(
		this, &UDroneHttpClient::HandleResponse, OnComplete);
	Req->ProcessRequest();
}

void UDroneHttpClient::Post(const FString& Path, const FString& JsonBody, FOnHttpResponse OnComplete)
{
	auto Req = MakeRequest(TEXT("POST"), Path, JsonBody);
	Req->OnProcessRequestComplete().BindUObject(
		this, &UDroneHttpClient::HandleResponse, OnComplete);
	Req->ProcessRequest();
}

void UDroneHttpClient::Put(const FString& Path, const FString& JsonBody, FOnHttpResponse OnComplete)
{
	auto Req = MakeRequest(TEXT("PUT"), Path, JsonBody);
	Req->OnProcessRequestComplete().BindUObject(
		this, &UDroneHttpClient::HandleResponse, OnComplete);
	Req->ProcessRequest();
}

void UDroneHttpClient::Delete(const FString& Path, FOnHttpResponse OnComplete)
{
	auto Req = MakeRequest(TEXT("DELETE"), Path, TEXT(""));
	Req->OnProcessRequestComplete().BindUObject(
		this, &UDroneHttpClient::HandleResponse, OnComplete);
	Req->ProcessRequest();
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UDroneHttpClient::MakeRequest(
	const FString& Verb, const FString& Path, const FString& Body)
{
	auto Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(Verb);
	Req->SetURL(BaseUrl + Path);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetTimeout(TimeoutSec);
	if (!Body.IsEmpty())
	{
		Req->SetContentAsString(Body);
	}
	return Req;
}

void UDroneHttpClient::HandleResponse(
	FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bConnected, FOnHttpResponse OnComplete)
{
	if (!bConnected || !Response.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneHttpClient] Request failed: %s"),
			Request.IsValid() ? *Request->GetURL() : TEXT("unknown"));
		OnComplete.ExecuteIfBound(false, TEXT(""));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();

	if (Code < 200 || Code >= 300)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneHttpClient] HTTP %d for %s: %s"),
			Code, *Request->GetURL(), *Body);
		OnComplete.ExecuteIfBound(false, Body);
		return;
	}

	OnComplete.ExecuteIfBound(true, Body);
}
