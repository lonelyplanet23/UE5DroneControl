#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Http.h"
#include "DroneHttpClient.generated.h"

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnHttpResponse, bool, bSuccess, const FString&, ResponseBody);

/**
 * Thin HTTP client for drone backend REST API.
 * Provides GET/POST/PUT/DELETE with unified JSON error handling.
 * Instantiate once (e.g. in DroneNetworkManager) and reuse.
 */
UCLASS(BlueprintType)
class UE5DRONECONTROL_API UDroneHttpClient : public UObject
{
	GENERATED_BODY()

public:
	/** Base URL of the backend, e.g. "http://127.0.0.1:8080" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString BaseUrl = TEXT("http://127.0.0.1:8080");

	/** Timeout in seconds for each request */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	float TimeoutSec = 5.0f;

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Get(const FString& Path, FOnHttpResponse OnComplete);

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Post(const FString& Path, const FString& JsonBody, FOnHttpResponse OnComplete);

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Put(const FString& Path, const FString& JsonBody, FOnHttpResponse OnComplete);

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Delete(const FString& Path, FOnHttpResponse OnComplete);

private:
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(
		const FString& Verb, const FString& Path, const FString& Body);

	void HandleResponse(
		FHttpRequestPtr Request, FHttpResponsePtr Response,
		bool bConnected, FOnHttpResponse OnComplete);
};
