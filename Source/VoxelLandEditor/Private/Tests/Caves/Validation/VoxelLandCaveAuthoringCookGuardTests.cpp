#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Misc/AutomationTest.h"
#include "Serialization/ArchiveSavePackageData.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ArchiveCookContext.h"
#include "UObject/ObjectSaveContext.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5AuthoringCookGuardTest, "VoxelLand.M5.Authoring.CookGuard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5AuthoringCookGuardTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	FObjectSaveContextData SaveData;
	SaveData.ObjectSaveContextPhase = EObjectSaveContextPhase::Write;
	FObjectSavePackageSerializeContext SerializeContext(SaveData);
	FArchiveCookContext CookContext(
		nullptr, UE::Cook::ECookType::Unknown, UE::Cook::ECookingDLC::Unknown, nullptr, nullptr);
	FArchiveSavePackageData CookData(SerializeContext, nullptr, &CookContext);
	Config->SetCookPreparationResult(true);
	for (int32 Mode = 0; Mode != 3; ++Mode)
	{
		Config->Caves.bEnabled = Mode != 1;
		Config->Caves.Pockets.bEnabled = Mode != 0;
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Writer.SetSavePackageData(&CookData);
		Config->EnforceCookPreparation(Writer);
		TestTrue(TEXT("enabled M5 rejects cook even after stale successful M4 preparation"), Writer.IsError());
	}
	Config->Caves.bEnabled = false;
	Config->Caves.Pockets.bEnabled = false;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer.SetSavePackageData(&CookData);
	Config->EnforceCookPreparation(Writer);
	TestFalse(TEXT("disabled M5 preserves prepared M4 cook behavior"), Writer.IsError());
	return true;
}
#endif
