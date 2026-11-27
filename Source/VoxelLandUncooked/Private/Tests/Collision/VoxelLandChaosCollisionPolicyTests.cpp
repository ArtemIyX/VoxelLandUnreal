#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosCollisionPolicyTest, "VoxelLand.Collision.Chaos.Policy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosCollisionPolicyTest::RunTest(const FString& Parameters)
{
	using EDetail = EVoxelLandChaosCollisionDetail;
	using EMode = EVoxelLandChaosCollisionOverrideMode;
	const UVoxelLandWorldConfig* Config = GetDefault<UVoxelLandWorldConfig>();
	TestFalse(TEXT("Unauthored World Config disables Chaos collision"), Config->ChaosCollision.bEnabled);

	FVoxelLandChaosCollisionSettings Settings = Config->ChaosCollision;
	FString Error;
	TestTrue(TEXT("Default settings are valid for opt-in"), Settings.Validate(Error));
	TestEqual(TEXT("The world toggle overrides all detail choices"), Settings.ResolveDetail(0), EDetail::Disabled);
	Settings.bEnabled = true;
	for (int32 RenderLod = INDEX_NONE; RenderLod < FVoxelLandChaosCollisionSettings::RenderLodCount; ++RenderLod)
	{
		TestEqual(TEXT("Fixed LOD 0 is independent of rendering"), Settings.ResolveDetail(RenderLod), EDetail::LOD0);
	}
	Settings.bMapByRenderLod = true;
	Settings.RenderLodDetails[0] = EDetail::LOD1;
	Settings.UnresolvedRenderDetail = EDetail::LOD2;
	TestEqual(TEXT("Fine visual tile can request a coarser collider"), Settings.ResolveDetail(0), EDetail::LOD1);
	TestEqual(TEXT("Visual LOD 1 can disable collision"), Settings.ResolveDetail(1), EDetail::Disabled);
	TestEqual(TEXT("Visual LOD 2 can disable collision"), Settings.ResolveDetail(2), EDetail::Disabled);
	TestEqual(TEXT("No visual publication uses the explicit fallback"), Settings.ResolveDetail(), EDetail::LOD2);

	FVoxelLandChaosCollisionOverride Override;
	TestEqual(TEXT("Inherit preserves the map"), Settings.ResolveDetail(0, &Override), EDetail::LOD1);
	Override.Mode = EMode::Disabled;
	TestEqual(TEXT("Disabled tile clears an active map rule"), Settings.ResolveDetail(0, &Override), EDetail::Disabled);
	Override.Mode = EMode::Enabled;
	TestEqual(TEXT("Explicit enable restores the fixed detail in an opted-out tile"),
		Settings.ResolveDetail(2, &Override), EDetail::LOD0);
	Override.bOverrideDetail = true;
	Override.Detail = EDetail::LOD2;
	TestEqual(TEXT("Explicit detail wins for an opted-in tile"), Settings.ResolveDetail(2, &Override), EDetail::LOD2);
	Settings.bEnabled = false;
	TestEqual(TEXT("Explicit override cannot enable a disabled world"), Settings.ResolveDetail(2, &Override),
		EDetail::Disabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosCollisionSettingsTest, "VoxelLand.Collision.Chaos.SettingsValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosCollisionSettingsTest::RunTest(const FString& Parameters)
{
	using EDetail = EVoxelLandChaosCollisionDetail;
	using EMode = EVoxelLandChaosCollisionOverrideMode;
	FVoxelLandChaosCollisionSettings Settings;
	FString Error;
	Settings.bEnabled = true;
	Settings.RenderLodDetails.Pop();
	TestFalse(TEXT("Truncated visual map is rejected"), Settings.Validate(Error));
	TestFalse(TEXT("Invalid mapping gives a diagnostic"), Error.IsEmpty());
	Settings.RenderLodDetails.Add(static_cast<EDetail>(255));
	TestFalse(TEXT("Unknown detail is rejected"), Settings.Validate(Error));
	Settings.RenderLodDetails.Last() = EDetail::Disabled;
	Settings.CollisionMode = ECollisionEnabled::NoCollision;
	TestFalse(TEXT("NoCollision requires using the world toggle"), Settings.Validate(Error));
	Settings.CollisionMode = ECollisionEnabled::QueryOnly;
	Settings.MaxConcurrentCooks = 0;
	TestFalse(TEXT("Zero concurrent cooks is rejected"), Settings.Validate(Error));
	Settings.MaxConcurrentCooks = 1;
	Settings.MaxPendingCookBytes = -1;
	TestFalse(TEXT("Negative pending cook bytes are rejected"), Settings.Validate(Error));
	Settings.MaxPendingCookBytes = 1024;
	FVoxelLandChaosCollisionResponse Response;
	Response.Channel = TEXT("Pawn");
	Response.Response = ECR_Overlap;
	Settings.Responses.Add(Response);
	TestFalse(TEXT("Overlap responses are rejected"), Settings.Validate(Error));
	Response.Response = ECR_Ignore;
	Settings.Responses[0] = Response;
	Settings.Responses.Add(Response);
	TestFalse(TEXT("Duplicate response channels are rejected"), Settings.Validate(Error));
	Settings.Responses.Pop();
	TestTrue(TEXT("A valid response and budget are accepted"), Settings.Validate(Error));
	TestTrue(TEXT("Successful validation clears prior errors"), Error.IsEmpty());

	FVoxelLandChaosCollisionOverride Source;
	Source.Mode = EMode::Enabled;
	Source.bOverrideDetail = true;
	Source.Detail = EDetail::Disabled;
	TestFalse(TEXT("Enabled cannot select Disabled as an override detail"), Source.IsValid());
	Source.Detail = EDetail::LOD1;
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		FStructuredArchiveFromArchive Structured(Archive);
		FVoxelLandChaosCollisionOverride::StaticStruct()->SerializeItem(Structured.GetSlot(), &Source, nullptr);
	}
	FVoxelLandChaosCollisionOverride Loaded;
	{
		FMemoryReader Reader(Bytes);
		FObjectAndNameAsStringProxyArchive Archive(Reader, false);
		FStructuredArchiveFromArchive Structured(Archive);
		FVoxelLandChaosCollisionOverride::StaticStruct()->SerializeItem(Structured.GetSlot(), &Loaded, nullptr);
		TestFalse(TEXT("Reading the override completes without a serialization error"), Archive.IsError());
	}
	TestTrue(TEXT("Override mode and detail survive tagged serialization"), Loaded == Source);
	return true;
}

#endif
