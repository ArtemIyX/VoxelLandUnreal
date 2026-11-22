#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandFiniteWorldAssetActions.h"
#include "VoxelLandFiniteWorldFactory.h"
#include "VoxelLandInfiniteWorldAssetActions.h"
#include "VoxelLandInfiniteWorldFactory.h"

#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandInfiniteWorld.h"
#include "Data/Enums/VoxelLandWorldKind.h"
#include "Data/Structs/VoxelLandFiniteWorldSettings.h"
#include "Data/Structs/VoxelLandInfiniteWorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldAssetEditorTest, "VoxelLand.WorldModes.Editor.AssetTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldAssetEditorTest::RunTest(const FString& Parameters)
{
	UVoxelLandFiniteWorldFactory* FiniteFactory = NewObject<UVoxelLandFiniteWorldFactory>();
	UVoxelLandInfiniteWorldFactory* InfiniteFactory = NewObject<UVoxelLandInfiniteWorldFactory>();
	if (!TestNotNull(TEXT("Finite factory"), FiniteFactory) || !TestNotNull(TEXT("Infinite factory"), InfiniteFactory))
	{
		return false;
	}

	UPackage* Parent = GetTransientPackage();
	const FName FiniteName = MakeUniqueObjectName(Parent, UVoxelLandFiniteWorld::StaticClass());
	const FName InfiniteName = MakeUniqueObjectName(Parent, UVoxelLandInfiniteWorld::StaticClass());
	UObject* FiniteObject = FiniteFactory->FactoryCreateNew(
		UVoxelLandFiniteWorld::StaticClass(), Parent, FiniteName, RF_Transient, nullptr, nullptr);
	UObject* InfiniteObject = InfiniteFactory->FactoryCreateNew(
		UVoxelLandInfiniteWorld::StaticClass(), Parent, InfiniteName, RF_Transient, nullptr, nullptr);
	UVoxelLandFiniteWorld* Finite = Cast<UVoxelLandFiniteWorld>(FiniteObject);
	UVoxelLandInfiniteWorld* Infinite = Cast<UVoxelLandInfiniteWorld>(InfiniteObject);
	if (!TestNotNull(TEXT("Finite factory creates a finite world"), Finite)
		|| !TestNotNull(TEXT("Infinite factory creates an infinite world"), Infinite))
	{
		return false;
	}
	TestTrue(TEXT("Finite creation is transactional"), Finite->HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("Infinite creation is transactional"), Infinite->HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("Finite asset determines finite mode"), Finite->GetWorldKind() == EVoxelLandWorldKind::Finite);
	TestTrue(
		TEXT("Infinite asset determines infinite mode"), Infinite->GetWorldKind() == EVoxelLandWorldKind::Infinite);
	TestNull(TEXT("Finite factory rejects the infinite class"),
		FiniteFactory->FactoryCreateNew(
			UVoxelLandInfiniteWorld::StaticClass(), Parent, NAME_None, RF_Transient, nullptr, nullptr));
	TestNull(TEXT("Infinite factory rejects the finite class"),
		InfiniteFactory->FactoryCreateNew(
			UVoxelLandFiniteWorld::StaticClass(), Parent, NAME_None, RF_Transient, nullptr, nullptr));
	TestNull(
		TEXT("Factory rejects a null parent"), FiniteFactory->FactoryCreateNew(UVoxelLandFiniteWorld::StaticClass(),
												   nullptr, NAME_None, RF_Transient, nullptr, nullptr));

	FVoxelLandFiniteWorldAssetActions FiniteActions;
	FVoxelLandInfiniteWorldAssetActions InfiniteActions;
	TestTrue(TEXT("Finite actions support only the finite class"),
		FiniteActions.GetSupportedClass() == UVoxelLandFiniteWorld::StaticClass());
	TestTrue(TEXT("Infinite actions support only the infinite class"),
		InfiniteActions.GetSupportedClass() == UVoxelLandInfiniteWorld::StaticClass());
	TestNotEqual(TEXT("World assets have distinct names"), FiniteActions.GetName().ToString(),
		InfiniteActions.GetName().ToString());
	TestTrue(TEXT("World assets have distinct colors"), FiniteActions.GetTypeColor() != InfiniteActions.GetTypeColor());
	const FProperty* FiniteSettings = FindFProperty<FProperty>(UVoxelLandFiniteWorld::StaticClass(), TEXT("Finite"));
	if (TestNotNull(TEXT("Finite settings property exists"), FiniteSettings))
	{
		TestTrue(TEXT("Finite settings are editable"), FiniteSettings->HasAnyPropertyFlags(CPF_Edit));
	}
	TestNull(TEXT("Infinite world has no finite settings property"),
		FindFProperty<FProperty>(UVoxelLandInfiniteWorld::StaticClass(), TEXT("Finite")));
	TestNotNull(TEXT("Finite settings expose global erosion"),
		FindFProperty<FProperty>(FVoxelLandFiniteWorldSettings::StaticStruct(), TEXT("bGlobalErosion")));
	TestNull(TEXT("Infinite settings have no global erosion property"),
		FindFProperty<FProperty>(FVoxelLandInfiniteWorldSettings::StaticStruct(), TEXT("bGlobalErosion")));
	return true;
}

#endif
