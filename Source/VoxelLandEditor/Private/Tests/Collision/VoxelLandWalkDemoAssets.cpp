#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Demo/VoxelLandClipmapDemo.h"
#include "World/Demo/VoxelLandSdfWalkPawn.h"

class FVoxelLandWalkDemoBuilder
{
public:
	static bool Create(FAutomationTestBase& Test)
	{
		const FString Filename =
			FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
		IFileManager& Files = IFileManager::Get();
		if (Files.FileExists(*Filename))
		{
			Test.AddError(TEXT("The editable SDF walking example already exists."));
			return false;
		}
		const FString Directory = FPaths::GetPath(Filename);
		if (!Files.DirectoryExists(*Directory) && !Files.MakeDirectory(*Directory, true))
		{
			Test.AddError(TEXT("Could not create the walking example directory."));
			return false;
		}

		const UWorld* Source =
			LoadObject<UWorld>(nullptr, TEXT("/VoxelLand/Examples/OutdoorLighting/OutdoorLighting.OutdoorLighting"));
		if (!Test.TestNotNull(TEXT("Outdoor lighting source level"), Source))
		{
			return false;
		}
		const TStrongObjectPtr<UWorld> World(
			DuplicateObject<UWorld>(Source, CreatePackage(MapPath), TEXT("SdfWalking")));
		if (!Test.TestNotNull(TEXT("Outdoor walking level copy"), World.Get()))
		{
			return false;
		}
		World->WorldType = EWorldType::Editor;
		if (!World->IsInitialized())
		{
			UWorld::InitializationValues Init;
			Init.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World->InitWorld(Init);
		}
		if (!AddPawn(Test, *World.Get()))
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);
		UPackage* Package = World->GetOutermost();
		Package->SetPackageFlags(PKG_ContainsMap);
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!Test.TestTrue(
				TEXT("Save the SDF walking level"), UPackage::SavePackage(Package, World.Get(), *Filename, Args)))
		{
			return false;
		}
		FAssetRegistryModule::AssetCreated(World.Get());
		return true;
	}

private:
	static constexpr const TCHAR* MapPath = TEXT("/VoxelLand/Examples/Collision/SdfWalking");

	static bool AddPawn(FAutomationTestBase& Test, UWorld& World)
	{
		AVoxelLandClipmapDemo* Terrain = nullptr;
		const APlayerStart* PlayerStart = nullptr;
		for (AActor* Actor : World.PersistentLevel->Actors)
		{
			if (AVoxelLandClipmapDemo* Candidate = Cast<AVoxelLandClipmapDemo>(Actor))
			{
				Terrain = Candidate;
			}
			else if (const APlayerStart* Start = Cast<APlayerStart>(Actor))
			{
				PlayerStart = Start;
			}
		}
		if (!Test.TestNotNull(TEXT("Copied level terrain"), Terrain)
			|| !Test.TestNotNull(TEXT("Copied level flight start"), PlayerStart))
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// The flight start can intersect the headroom of a walking capsule.
		const FVector Start = PlayerStart->GetActorLocation() + FVector(0.0, 0.0, 3400.0);
		AVoxelLandSdfWalkPawn* Pawn =
			World.SpawnActor<AVoxelLandSdfWalkPawn>(Start, PlayerStart->GetActorRotation(), Spawn);
		if (!Test.TestNotNull(TEXT("Collision walking pawn"), Pawn))
		{
			return false;
		}
		Pawn->TerrainWorld = Terrain;
		Pawn->SetActorLabel(TEXT("SDF Walking Pawn"));
		return true;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCreateWalkDemoTest, "VoxelLand.Collision.CreateWalkDemo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCreateWalkDemoTest::RunTest(const FString& Parameters)
{
	return !FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateWalkDemo"))
		|| FVoxelLandWalkDemoBuilder::Create(*this);
}

#endif
