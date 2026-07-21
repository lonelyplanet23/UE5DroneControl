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

	/** Cancel all in-flight HTTP requests used by strict local preview isolation. */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void CancelAllPendingRequests();

	/** Number of HTTP requests currently in flight. */
	UFUNCTION(BlueprintPure, Category = "Network")
	int32 GetPendingRequestCount() const;

	/**
	 * POST /api/drones/refresh — 探测所有断连无人机。
	 * 回调参数：(bool bSuccess, TArray<int32> RefreshedIds)
	 * bSuccess=false 时 RefreshedIds 为空；bSuccess=true 且 RefreshedIds 为空表示无断连无人机。
	 */
	void PostRefresh(TFunction<void(bool, const TArray<int32>&)> Callback);

private:
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(
		const FString& Verb, const FString& Path, const FString& Body);

	/** Track an in-flight request for later cancellation. */
	void TrackRequest(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request);

	/** Remove completed requests from the tracking list. */
	void PruneCompletedRequests();

	void HandleResponse(
		FHttpRequestPtr Request, FHttpResponsePtr Response,
		bool bConnected, FOnHttpResponse OnComplete);

	/** All in-flight HTTP requests, pruned on each new request and on cancel. */
	TArray<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> InFlightRequests;
};
