#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "PathDragDropOperation.generated.h"

UCLASS()
class UE5DRONECONTROL_API UPathDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	int32 SourcePathId = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	int32 SourcePathIndex = INDEX_NONE;
};
