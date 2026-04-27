#include "DronePathSaveLibrary.h"

#include "DronePathActor.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogDronePathSaveLibrary, Log, All);

bool UDronePathSaveLibrary::SavePathsToJson(const TMap<int32, ADronePathActor*>& PathMap, const FString& FileName)
{
	if (PathMap.IsEmpty())
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsToJson failed because PathMap is empty."));
		return false;
	}

	FDronePathsSaveData SaveData;
	SaveData.Paths.Reserve(PathMap.Num());

	for (const TPair<int32, ADronePathActor*>& PathPair : PathMap)
	{
		ADronePathActor* PathActor = PathPair.Value;
		if (!IsValid(PathActor))
		{
			continue;
		}

		if (PathActor->Waypoints.IsEmpty())
		{
			continue;
		}

		FDronePathSaveData& PathSaveData = SaveData.Paths.AddDefaulted_GetRef();
		PathSaveData.PathId = (PathActor->GetPathNumericId() != INDEX_NONE) ? PathActor->GetPathNumericId() : PathPair.Key;
		PathSaveData.bClosedLoop = PathActor->bClosedLoop;
		PathSaveData.Waypoints.Reserve(PathActor->Waypoints.Num());

		for (int32 WaypointIndex = 0; WaypointIndex < PathActor->Waypoints.Num(); ++WaypointIndex)
		{
			const FDroneWaypoint& Waypoint = PathActor->Waypoints[WaypointIndex];

			FDroneWaypointSaveData& WaypointSaveData = PathSaveData.Waypoints.AddDefaulted_GetRef();
			WaypointSaveData.Location = PathActor->GetWaypointWorldLocation(WaypointIndex);
			WaypointSaveData.SegmentSpeed = Waypoint.SegmentSpeed;
			WaypointSaveData.WaitTime = Waypoint.WaitTime;
		}
	}

	if (SaveData.Paths.IsEmpty())
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsToJson failed because no valid non-empty paths were available to save."));
		return false;
	}

	const FString SanitizedFileName = FPaths::MakeValidFileName(FileName.IsEmpty() ? TEXT("DronePaths") : FileName);
	const FString SaveDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DronePaths"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*SaveDirectory) && !PlatformFile.CreateDirectoryTree(*SaveDirectory))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("SavePathsToJson failed to create directory: %s"), *SaveDirectory);
		return false;
	}

	TSharedRef<FJsonObject> RootJsonObject = MakeShared<FJsonObject>();
	if (!FJsonObjectConverter::UStructToJsonObject(FDronePathsSaveData::StaticStruct(), &SaveData, RootJsonObject, 0, 0))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("SavePathsToJson failed to convert save data to JSON object."));
		return false;
	}

	FString JsonOutput;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonOutput);
	if (!FJsonSerializer::Serialize(RootJsonObject, JsonWriter))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("SavePathsToJson failed to serialize JSON output."));
		return false;
	}

	const FString SaveFilePath = FPaths::Combine(SaveDirectory, FString::Printf(TEXT("%s.json"), *SanitizedFileName));
	if (!FFileHelper::SaveStringToFile(JsonOutput, *SaveFilePath))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("SavePathsToJson failed to write file: %s"), *SaveFilePath);
		return false;
	}

	return true;
}

bool UDronePathSaveLibrary::LoadPathsFromJson(const FString& FilePath, FDronePathsSaveData& OutData)
{
	OutData = FDronePathsSaveData();

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("LoadPathsFromJson failed because file was not found: %s"), *FilePath);
		return false;
	}

	FString JsonInput;
	if (!FFileHelper::LoadFileToString(JsonInput, *FilePath))
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("LoadPathsFromJson failed to read file: %s"), *FilePath);
		return false;
	}

	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonInput, &OutData, 0, 0))
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("LoadPathsFromJson failed to parse JSON: %s"), *FilePath);
		OutData = FDronePathsSaveData();
		return false;
	}

	return true;
}
