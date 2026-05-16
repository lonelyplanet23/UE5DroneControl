#include "PathFileListItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UPathFileListItemWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SelectButton)
	{
		SelectButton->OnClicked.AddDynamic(this, &UPathFileListItemWidget::HandleButtonClicked);
	}
}

void UPathFileListItemWidget::SetFileInfo(const FString& InFileName, const FString& InFilePath)
{
	FilePath = InFilePath;
	if (FileNameText)
	{
		FileNameText->SetText(FText::FromString(InFileName));
	}
}

void UPathFileListItemWidget::HandleButtonClicked()
{
	OnClicked.ExecuteIfBound(FilePath);
}
