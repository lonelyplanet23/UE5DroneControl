// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DroneOps/Core/GeographicTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGeographicCoordinateTextParserTest,
	"DroneOps.WGS84.CoordinateVectorParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeographicCoordinateTextParserTest::RunTest(const FString& Parameters)
{
	auto TestValid = [this](const TCHAR* Label, const FString& Text, double Longitude, double Latitude, double Altitude)
	{
		FGeographicCoordinate3D Coordinate;
		FString Error;
		const bool bParsed = FGeographicCoordinateTextParser::Parse(Text, Coordinate, Error);
		TestTrue(Label, bParsed);
		if (bParsed)
		{
			TestTrue(TEXT("longitude matches"), FMath::IsNearlyEqual(Coordinate.Longitude, Longitude, 1e-9));
			TestTrue(TEXT("latitude matches"), FMath::IsNearlyEqual(Coordinate.Latitude, Latitude, 1e-9));
			TestTrue(TEXT("altitude matches"), FMath::IsNearlyEqual(Coordinate.AltitudeMslMeters, Altitude, 1e-9));
		}
	};

	auto TestInvalid = [this](const TCHAR* Label, const FString& Text)
	{
		FGeographicCoordinate3D Coordinate;
		FString Error;
		TestFalse(Label, FGeographicCoordinateTextParser::Parse(Text, Coordinate, Error));
		TestFalse(TEXT("invalid input includes a user-facing error"), Error.IsEmpty());
	};

	TestValid(TEXT("ASCII tuple can be pasted"), TEXT("(116.347, 39.981, 63)"), 116.347, 39.981, 63.0);
	TestValid(TEXT("Chinese punctuation can be pasted"), TEXT("（-73.5，40.25；-12.75）"), -73.5, 40.25, -12.75);
	TestValid(TEXT("whitespace vector can be pasted"), TEXT("180 -90 0"), 180.0, -90.0, 0.0);
	TestValid(TEXT("square brackets can be pasted"), TEXT("[0, 90, 100.5]"), 0.0, 90.0, 100.5);

	TestInvalid(TEXT("empty vector is rejected"), TEXT(""));
	TestInvalid(TEXT("missing component is rejected"), TEXT("(116.3, 39.9)"));
	TestInvalid(TEXT("extra component is rejected"), TEXT("(116.3, 39.9, 50, 1)"));
	TestInvalid(TEXT("invalid longitude is rejected"), TEXT("(180.0001, 0, 0)"));
	TestInvalid(TEXT("invalid latitude is rejected"), TEXT("(0, -90.0001, 0)"));
	TestInvalid(TEXT("non-numeric component is rejected"), TEXT("(x, 0, 0)"));

	FGeographicCoordinate3D Source;
	Source.Longitude = 116.347;
	Source.Latitude = 39.981;
	Source.AltitudeMslMeters = 63.25;
	FGeographicCoordinate3D RoundTrip;
	FString Error;
	TestTrue(TEXT("formatted vector parses again"),
		FGeographicCoordinateTextParser::Parse(FGeographicCoordinateTextParser::Format(Source), RoundTrip, Error));
	TestTrue(TEXT("formatted longitude retains precision"), FMath::IsNearlyEqual(Source.Longitude, RoundTrip.Longitude, 1e-9));
	TestTrue(TEXT("formatted latitude retains precision"), FMath::IsNearlyEqual(Source.Latitude, RoundTrip.Latitude, 1e-9));
	TestTrue(TEXT("formatted altitude retains centimetres"), FMath::IsNearlyEqual(Source.AltitudeMslMeters, RoundTrip.AltitudeMslMeters, 0.005));

	auto TestBatchValid = [this](const TCHAR* Label, const FString& Text)
	{
		TArray<FGeographicCoordinate3D> Coordinates;
		FString BatchError;
		const bool bParsed = FGeographicCoordinateTextParser::ParseBatch(Text, Coordinates, BatchError);
		TestTrue(Label, bParsed);
		TestEqual(TEXT("batch contains two ordered tuples"), Coordinates.Num(), 2);
		if (Coordinates.Num() == 2)
		{
			TestTrue(TEXT("first tuple longitude is retained"),
				FMath::IsNearlyEqual(Coordinates[0].Longitude, 116.397, 1e-9));
			TestTrue(TEXT("first tuple latitude is retained"),
				FMath::IsNearlyEqual(Coordinates[0].Latitude, 39.908, 1e-9));
			TestTrue(TEXT("first tuple altitude is retained"),
				FMath::IsNearlyEqual(Coordinates[0].AltitudeMslMeters, 50.0, 1e-9));
			TestTrue(TEXT("second tuple longitude is retained"),
				FMath::IsNearlyEqual(Coordinates[1].Longitude, 116.398, 1e-9));
			TestTrue(TEXT("second tuple latitude is retained"),
				FMath::IsNearlyEqual(Coordinates[1].Latitude, 39.909, 1e-9));
			TestTrue(TEXT("second tuple altitude is retained"),
				FMath::IsNearlyEqual(Coordinates[1].AltitudeMslMeters, 52.0, 1e-9));
		}
	};

	auto TestBatchInvalid = [this](const TCHAR* Label, const FString& Text, const TCHAR* ExpectedError)
	{
		TArray<FGeographicCoordinate3D> Coordinates;
		Coordinates.AddDefaulted();
		FString BatchError;
		TestFalse(Label, FGeographicCoordinateTextParser::ParseBatch(Text, Coordinates, BatchError));
		TestTrue(TEXT("batch error identifies tuple and reason"), BatchError.Contains(ExpectedError));
		TestEqual(TEXT("failed batch exposes no partial coordinates"), Coordinates.Num(), 0);
	};

	TestBatchValid(TEXT("adjacent tuples preserve order"),
		TEXT("(116.397,39.908,50)(116.398,39.909,52)"));
	TestBatchValid(TEXT("spaces and newline between tuples are accepted"),
		TEXT("(116.397, 39.908, 50)\n  (116.398,39.909,52)"));

	TArray<FGeographicCoordinate3D> SignedCoordinates;
	FString SignedError;
	TestTrue(TEXT("signed decimal values are accepted"), FGeographicCoordinateTextParser::ParseBatch(
		TEXT("(-73.500,+40.250,-12.75)"), SignedCoordinates, SignedError));

	TestBatchInvalid(TEXT("missing opening parenthesis rejects the whole batch"),
		TEXT("116.397,39.908,50"), TEXT("第 1 个坐标缺少左括号"));
	TestBatchInvalid(TEXT("missing closing parenthesis identifies the second tuple"),
		TEXT("(116.397,39.908,50)(116.398,39.909,52"), TEXT("第 2 个坐标缺少右括号"));
	TestBatchInvalid(TEXT("missing field identifies the second tuple"),
		TEXT("(116.397,39.908,50)(116.398,39.909)"), TEXT("第 2 个坐标字段不足"));
	TestBatchInvalid(TEXT("non-numeric field identifies the second tuple"),
		TEXT("(116.397,39.908,50)(116.398,abc,52)"), TEXT("第 2 个坐标纬度不是有效数字"));
	TestBatchInvalid(TEXT("longitude range identifies the second tuple"),
		TEXT("(116.397,39.908,50)(180.1,39.909,52)"), TEXT("第 2 个坐标经度超出 [-180, 180]"));
	TestBatchInvalid(TEXT("latitude range identifies the second tuple"),
		TEXT("(116.397,39.908,50)(116.398,-90.1,52)"), TEXT("第 2 个坐标纬度超出 [-90, 90]"));
	TestBatchInvalid(TEXT("illegal text between tuples is never skipped"),
		TEXT("(116.397,39.908,50)junk(116.398,39.909,52)"),
		TEXT("第 2 个坐标缺少左括号或元组之间存在非法字符"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
