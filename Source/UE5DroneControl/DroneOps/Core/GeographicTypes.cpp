// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeographicTypes.h"

#include "Misc/DefaultValueHelper.h"

namespace
{
const TCHAR* CoordinateFieldName(int32 Index)
{
	static const TCHAR* Names[] = {TEXT("经度"), TEXT("纬度"), TEXT("海拔")};
	return Names[FMath::Clamp(Index, 0, 2)];
}

bool HasMatchingOuterBrackets(const FString& Text)
{
	if (Text.Len() < 2)
	{
		return false;
	}

	const TCHAR First = Text[0];
	const TCHAR Last = Text[Text.Len() - 1];
	return (First == TEXT('(') && Last == TEXT(')'))
		|| (First == TEXT('[') && Last == TEXT(']'))
		|| (First == TEXT('{') && Last == TEXT('}'))
		|| (First == TEXT('（') && Last == TEXT('）'));
}

void NormalizeCoordinateSeparators(FString& Text)
{
	Text.ReplaceInline(TEXT("，"), TEXT(","));
	Text.ReplaceInline(TEXT("；"), TEXT(","));
	Text.ReplaceInline(TEXT(";"), TEXT(","));
}

bool ParseFiniteDoubleStrict(const FString& Text, double& OutValue)
{
	int32 Cursor = 0;
	if (Cursor < Text.Len() && (Text[Cursor] == TEXT('+') || Text[Cursor] == TEXT('-')))
	{
		++Cursor;
	}

	bool bHasMantissaDigit = false;
	while (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor]))
	{
		bHasMantissaDigit = true;
		++Cursor;
	}
	if (Cursor < Text.Len() && Text[Cursor] == TEXT('.'))
	{
		++Cursor;
		while (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor]))
		{
			bHasMantissaDigit = true;
			++Cursor;
		}
	}
	if (!bHasMantissaDigit)
	{
		return false;
	}

	if (Cursor < Text.Len() && (Text[Cursor] == TEXT('e') || Text[Cursor] == TEXT('E')))
	{
		++Cursor;
		if (Cursor < Text.Len() && (Text[Cursor] == TEXT('+') || Text[Cursor] == TEXT('-')))
		{
			++Cursor;
		}
		const int32 ExponentStart = Cursor;
		while (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor]))
		{
			++Cursor;
		}
		if (Cursor == ExponentStart)
		{
			return false;
		}
	}

	return Cursor == Text.Len()
		&& FDefaultValueHelper::ParseDouble(Text, OutValue)
		&& FMath::IsFinite(OutValue);
}

bool ParseCoordinateComponents(
	const FString& CoordinateText,
	FGeographicCoordinate3D& OutCoordinate,
	FString& OutError)
{
	FString Normalized = CoordinateText.TrimStartAndEnd();
	NormalizeCoordinateSeparators(Normalized);

	TArray<FString> Parts;
	if (Normalized.Contains(TEXT(",")))
	{
		Normalized.ParseIntoArray(Parts, TEXT(","), false);
	}
	else
	{
		Normalized.ParseIntoArrayWS(Parts);
	}

	for (FString& Part : Parts)
	{
		Part = Part.TrimStartAndEnd();
	}
	if (Parts.Num() < 3)
	{
		OutError = TEXT("字段不足，应为经度、纬度、海拔 3 项");
		return false;
	}
	if (Parts.Num() > 3)
	{
		OutError = TEXT("字段数量超过 3，应为经度、纬度、海拔 3 项");
		return false;
	}

	double Values[3] = {};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Parts[Index].IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s为空"), CoordinateFieldName(Index));
			return false;
		}
		if (!ParseFiniteDoubleStrict(Parts[Index], Values[Index]))
		{
			OutError = FString::Printf(TEXT("%s不是有效数字"), CoordinateFieldName(Index));
			return false;
		}
	}

	if (Values[0] < -180.0 || Values[0] > 180.0)
	{
		OutError = TEXT("经度超出 [-180, 180]");
		return false;
	}
	if (Values[1] < -90.0 || Values[1] > 90.0)
	{
		OutError = TEXT("纬度超出 [-90, 90]");
		return false;
	}

	OutCoordinate.Longitude = Values[0];
	OutCoordinate.Latitude = Values[1];
	OutCoordinate.AltitudeMslMeters = Values[2];
	return true;
}

bool IsTupleOpen(TCHAR Character, TCHAR& OutClose)
{
	if (Character == TEXT('('))
	{
		OutClose = TEXT(')');
		return true;
	}
	if (Character == TEXT('（'))
	{
		OutClose = TEXT('）');
		return true;
	}
	return false;
}

bool IsBatchWhitespace(TCHAR Character)
{
	return FChar::IsWhitespace(Character)
		|| FChar::IsLinebreak(Character)
		|| Character == TEXT('\r');
}
}

bool FGeographicCoordinateTextParser::Parse(
	const FString& Text,
	FGeographicCoordinate3D& OutCoordinate,
	FString& OutError)
{
	OutCoordinate = FGeographicCoordinate3D();
	OutError.Reset();

	FString Normalized = Text.TrimStartAndEnd();
	if (Normalized.IsEmpty())
	{
		OutError = TEXT("请输入目标坐标，格式为（经度, 纬度, 海拔）");
		return false;
	}

	if (HasMatchingOuterBrackets(Normalized))
	{
		Normalized = Normalized.Mid(1, Normalized.Len() - 2).TrimStartAndEnd();
	}

	return ParseCoordinateComponents(Normalized, OutCoordinate, OutError);
}

bool FGeographicCoordinateTextParser::ParseBatch(
	const FString& Text,
	TArray<FGeographicCoordinate3D>& OutCoordinates,
	FString& OutError)
{
	OutCoordinates.Reset();
	OutError.Reset();

	const FString Input = Text.TrimStartAndEnd();
	if (Input.IsEmpty())
	{
		OutError = TEXT("请输入至少一组批量坐标");
		return false;
	}

	int32 Cursor = 0;
	int32 CoordinateIndex = 1;
	while (Cursor < Input.Len())
	{
		while (Cursor < Input.Len() && IsBatchWhitespace(Input[Cursor]))
		{
			++Cursor;
		}
		if (Cursor >= Input.Len())
		{
			break;
		}

		TCHAR ExpectedClose = 0;
		if (!IsTupleOpen(Input[Cursor], ExpectedClose))
		{
			OutError = FString::Printf(
				TEXT("第 %d 个坐标缺少左括号或元组之间存在非法字符"), CoordinateIndex);
			OutCoordinates.Reset();
			return false;
		}

		const int32 ContentStart = Cursor + 1;
		int32 CloseIndex = INDEX_NONE;
		for (int32 Scan = ContentStart; Scan < Input.Len(); ++Scan)
		{
			if (Input[Scan] == ExpectedClose)
			{
				CloseIndex = Scan;
				break;
			}
		}
		if (CloseIndex == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("第 %d 个坐标缺少右括号"), CoordinateIndex);
			OutCoordinates.Reset();
			return false;
		}

		FGeographicCoordinate3D Coordinate;
		FString CoordinateError;
		if (!ParseCoordinateComponents(
			Input.Mid(ContentStart, CloseIndex - ContentStart), Coordinate, CoordinateError))
		{
			OutError = FString::Printf(TEXT("第 %d 个坐标%s"), CoordinateIndex, *CoordinateError);
			OutCoordinates.Reset();
			return false;
		}

		OutCoordinates.Add(Coordinate);
		Cursor = CloseIndex + 1;
		++CoordinateIndex;
	}

	if (OutCoordinates.IsEmpty())
	{
		OutError = TEXT("请输入至少一组批量坐标");
		return false;
	}
	return true;
}

FString FGeographicCoordinateTextParser::Format(const FGeographicCoordinate3D& Coordinate)
{
	return FString::Printf(
		TEXT("(%.9f, %.9f, %.2f)"),
		Coordinate.Longitude,
		Coordinate.Latitude,
		Coordinate.AltitudeMslMeters);
}

bool FGeographicDispatchOffsetCalculator::CalculateBackendRelativeOffset(
	const FGeographicCoordinate3D& TargetCoordinate,
	const FGeographicCoordinate3D& CurrentCoordinate,
	const FVector& CurrentLocalUeOffset,
	const FVector& AdditionalLocalUeOffset,
	FVector& OutRelativeOffset)
{
	OutRelativeOffset = FVector::ZeroVector;
	const auto IsValidCoordinate = [](const FGeographicCoordinate3D& Coordinate)
	{
		return FMath::IsFinite(Coordinate.Longitude)
			&& Coordinate.Longitude >= -180.0 && Coordinate.Longitude <= 180.0
			&& FMath::IsFinite(Coordinate.Latitude)
			&& Coordinate.Latitude >= -90.0 && Coordinate.Latitude <= 90.0
			&& FMath::IsFinite(Coordinate.AltitudeMslMeters);
	};
	const auto IsFiniteVector = [](const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	};
	if (!IsValidCoordinate(TargetCoordinate)
		|| !IsValidCoordinate(CurrentCoordinate)
		|| !IsFiniteVector(CurrentLocalUeOffset)
		|| !IsFiniteVector(AdditionalLocalUeOffset))
	{
		return false;
	}

	// WGS84 local radii of curvature. Geographic drone targets are local
	// movements, so the tangent-plane delta is both stable and sufficiently
	// precise while avoiding any Cesium-world/PX4-axis coupling.
	constexpr double SemiMajorMeters = 6378137.0;
	constexpr double Flattening = 1.0 / 298.257223563;
	constexpr double EccentricitySquared = Flattening * (2.0 - Flattening);
	const double CurrentLatitude = FMath::DegreesToRadians(CurrentCoordinate.Latitude);
	const double TargetLatitude = FMath::DegreesToRadians(TargetCoordinate.Latitude);
	const double MeanLatitude = 0.5 * (CurrentLatitude + TargetLatitude);
	const double SinMeanLatitude = FMath::Sin(MeanLatitude);
	const double CurvatureTerm = 1.0
		- EccentricitySquared * SinMeanLatitude * SinMeanLatitude;
	if (CurvatureTerm <= 0.0 || !FMath::IsFinite(CurvatureTerm))
	{
		return false;
	}
	const double PrimeVerticalRadius = SemiMajorMeters / FMath::Sqrt(CurvatureTerm);
	const double MeridionalRadius = SemiMajorMeters * (1.0 - EccentricitySquared)
		/ FMath::Pow(CurvatureTerm, 1.5);
	const double RawLongitudeDelta = FMath::DegreesToRadians(
		TargetCoordinate.Longitude - CurrentCoordinate.Longitude);
	const double ShortLongitudeDelta = FMath::Atan2(
		FMath::Sin(RawLongitudeDelta), FMath::Cos(RawLongitudeDelta));
	const double NorthMeters = (TargetLatitude - CurrentLatitude) * MeridionalRadius;
	const double EastMeters = ShortLongitudeDelta * PrimeVerticalRadius * FMath::Cos(MeanLatitude);

	OutRelativeOffset.X = CurrentLocalUeOffset.X + NorthMeters * 100.0 + AdditionalLocalUeOffset.X;
	OutRelativeOffset.Y = CurrentLocalUeOffset.Y + EastMeters * 100.0 + AdditionalLocalUeOffset.Y;
	OutRelativeOffset.Z = CurrentLocalUeOffset.Z
		+ (TargetCoordinate.AltitudeMslMeters - CurrentCoordinate.AltitudeMslMeters) * 100.0
		+ AdditionalLocalUeOffset.Z;
	if (!IsFiniteVector(OutRelativeOffset))
	{
		OutRelativeOffset = FVector::ZeroVector;
		return false;
	}
	return true;
}
