#include "DronePathSaveLibrary.h"

#include "DronePathActor.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogDronePathSaveLibrary, Log, All);

namespace
{
bool EnsureDronePathSaveDirectory(FString& OutSaveDirectory)
{
	const FString ProjectSavedDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	OutSaveDirectory = FPaths::Combine(ProjectSavedDirectory, TEXT("DronePaths"));

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*OutSaveDirectory) && !FileManager.MakeDirectory(*OutSaveDirectory, true))
	{
		TCHAR SystemError[1024] = { 0 };
		FPlatformMisc::GetSystemErrorMessage(SystemError, UE_ARRAY_COUNT(SystemError), FPlatformMisc::GetLastError());
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("Failed to create DronePaths directory. SaveDirectory='%s', SystemError='%s'"),
			*OutSaveDirectory,
			SystemError);
		return false;
	}

	if (!FileManager.DirectoryExists(*OutSaveDirectory))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("DronePaths directory is still missing after creation attempt. SaveDirectory='%s'"),
			*OutSaveDirectory);
		return false;
	}

	return true;
}

FString BuildDronePathSaveFilePath(const FString& SaveDirectory, const FString& FileName)
{
	const FString RequestedFileName = FileName.IsEmpty() ? TEXT("DronePaths") : FPaths::GetCleanFilename(FileName);
	FString SanitizedBaseName = FPaths::MakeValidFileName(FPaths::GetBaseFilename(RequestedFileName));
	if (SanitizedBaseName.IsEmpty())
	{
		SanitizedBaseName = TEXT("DronePaths");
	}

	return FPaths::Combine(SaveDirectory, SanitizedBaseName + TEXT(".json"));
}

bool WriteDronePathsSaveDataToJson(const FDronePathsSaveData& SaveData, const FString& SaveFilePath)
{
	TSharedRef<FJsonObject> RootJsonObject = MakeShared<FJsonObject>();
	if (!FJsonObjectConverter::UStructToJsonObject(FDronePathsSaveData::StaticStruct(), &SaveData, RootJsonObject, 0, 0))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("Failed to convert save data to JSON object. SaveFilePath='%s'"), *SaveFilePath);
		return false;
	}

	FString JsonOutput;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonOutput);
	if (!FJsonSerializer::Serialize(RootJsonObject, JsonWriter))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("Failed to serialize JSON output. SaveFilePath='%s'"), *SaveFilePath);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonOutput, *SaveFilePath))
	{
		TCHAR SystemError[1024] = { 0 };
		FPlatformMisc::GetSystemErrorMessage(SystemError, UE_ARRAY_COUNT(SystemError), FPlatformMisc::GetLastError());
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("Failed to write JSON file. SaveFilePath='%s', JsonLength=%d, SystemError='%s'"),
			*SaveFilePath,
			JsonOutput.Len(),
			SystemError);
		return false;
	}

	if (!FPaths::FileExists(SaveFilePath))
	{
		UE_LOG(LogDronePathSaveLibrary, Error, TEXT("JSON write reported success, but output file does not exist. SaveFilePath='%s'"), *SaveFilePath);
		return false;
	}

	UE_LOG(LogDronePathSaveLibrary, Log, TEXT("JSON save succeeded. SaveFilePath='%s', SavedPathCount=%d, JsonLength=%d, FileSize=%lld"),
		*SaveFilePath,
		SaveData.Paths.Num(),
		JsonOutput.Len(),
		IFileManager::Get().FileSize(*SaveFilePath));
	return true;
}
}

bool UDronePathSaveLibrary::SavePathsToJson(const TMap<int32, ADronePathActor*>& PathMap, const FString& FileName)
{
	const FString ProjectSavedDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	FString SaveDirectory;

	UE_LOG(LogDronePathSaveLibrary, Log, TEXT("SavePathsToJson requested. InputFileName='%s', PathCount=%d, ProjectSavedDir='%s', SaveDirectory='%s'"),
		*FileName,
		PathMap.Num(),
		*ProjectSavedDirectory,
		*FPaths::Combine(ProjectSavedDirectory, TEXT("DronePaths")));

	if (!EnsureDronePathSaveDirectory(SaveDirectory))
	{
		return false;
	}

	if (PathMap.IsEmpty())
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsToJson failed because PathMap is empty. SaveDirectory='%s'"), *SaveDirectory);
		return false;
	}

	FDronePathsSaveData SaveData;
	// 编辑关卡固定使用 DroneId=1 作为源锚点无人机。
	SaveData.AnchorDroneId = 1;
	SaveData.Paths.Reserve(PathMap.Num());

	for (const TPair<int32, ADronePathActor*>& PathPair : PathMap)
	{
		ADronePathActor* PathActor = PathPair.Value;
		if (!IsValid(PathActor))
		{
			UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsToJson skipped invalid PathActor for map key %d."), PathPair.Key);
			continue;
		}

		if (PathActor->Waypoints.IsEmpty())
		{
			UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsToJson skipped empty path '%s' for map key %d."), *PathActor->GetName(), PathPair.Key);
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
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsToJson failed because no valid non-empty paths were available to save. SaveDirectory='%s'"), *SaveDirectory);
		return false;
	}

	// 校验源锚点路径是否存在（PathId == AnchorDroneId 且有航点）。缺失时仅告警，不阻断保存。
	const bool bHasAnchorPath = SaveData.Paths.ContainsByPredicate(
		[&SaveData](const FDronePathSaveData& PathData)
		{
			return PathData.PathId == SaveData.AnchorDroneId && PathData.Waypoints.Num() > 0;
		});
	if (!bHasAnchorPath)
	{
		UE_LOG(LogDronePathSaveLibrary, Warning,
			TEXT("SavePathsToJson: source anchor path (DroneId=%d) is missing or empty. Rotation/translation in preview may fall back to the first path."),
			SaveData.AnchorDroneId);
	}

	const FString SaveFilePath = BuildDronePathSaveFilePath(SaveDirectory, FileName);
	return WriteDronePathsSaveDataToJson(SaveData, SaveFilePath);
}

bool UDronePathSaveLibrary::SaveAllPathsInWorldToJson(UObject* WorldContextObject, const FString& FileName)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SaveAllPathsInWorldToJson failed because WorldContextObject is null."));
		return false;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SaveAllPathsInWorldToJson failed because no world was available."));
		return false;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, ADronePathActor::StaticClass(), FoundActors);

	TMap<int32, ADronePathActor*> PathMap;
	int32 FallbackPathId = 1;
	for (AActor* Actor : FoundActors)
	{
		ADronePathActor* PathActor = Cast<ADronePathActor>(Actor);
		if (!IsValid(PathActor))
		{
			continue;
		}

		if (PathActor->Waypoints.IsEmpty())
		{
			UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SaveAllPathsInWorldToJson skipped empty path actor '%s'."), *PathActor->GetName());
			continue;
		}

		int32 PathId = PathActor->GetPathNumericId();
		if (PathId == INDEX_NONE)
		{
			PathId = FallbackPathId;
		}

		while (PathMap.Contains(PathId))
		{
			++FallbackPathId;
			PathId = FallbackPathId;
		}

		PathMap.Add(PathId, PathActor);
		++FallbackPathId;
	}

	UE_LOG(LogDronePathSaveLibrary, Log, TEXT("SaveAllPathsInWorldToJson collected %d valid paths from %d ADronePathActor instances."),
		PathMap.Num(),
		FoundActors.Num());

	if (PathMap.IsEmpty())
	{
		FString SaveDirectory;
		if (!EnsureDronePathSaveDirectory(SaveDirectory))
		{
			return false;
		}

		const FString SaveFilePath = BuildDronePathSaveFilePath(SaveDirectory, FileName);
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SaveAllPathsInWorldToJson found no valid paths. Writing empty JSON file: %s"), *SaveFilePath);
		return WriteDronePathsSaveDataToJson(FDronePathsSaveData(), SaveFilePath);
	}

	return SavePathsToJson(PathMap, FileName);
}

bool UDronePathSaveLibrary::SavePathsDataToFile(const FString& FilePath, const FDronePathsSaveData& Data)
{
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogDronePathSaveLibrary, Warning, TEXT("SavePathsDataToFile failed because FilePath is empty."));
		return false;
	}

	// 直接覆盖指定文件（不做 DronePaths 目录拼接）。复用统一的写盘逻辑。
	return WriteDronePathsSaveDataToJson(Data, FilePath);
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
