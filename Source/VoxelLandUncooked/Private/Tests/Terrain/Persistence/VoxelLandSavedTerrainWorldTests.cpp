#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Terrain/Persistence/VoxelLandSavedTerrainWorldRecord.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Class.h"
#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorld.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"

namespace
{
constexpr EAutomationTestFlags M4SavedWorldTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4SavedWorldTest, "VoxelLand.M4.SavedWorld.RoundTrip", M4SavedWorldTestFlags)
bool FVoxelLandM4SavedWorldTest::RunTest(const FString& Parameters)
{
	bool bCaptured = false, bRestored = false, bFields = false, bIdentity = false;
	bool bTampered = false, bUnknown = false, bAtomic = false, bBadProgram = false;
	bool bCaptureRadius = false, bCaptureOversized = false, bCaptureAtomic = false;
	bool bOutputIdentity = false, bCompiledIdentity = false, bScaleIdentity = false, bBaseIdentity = false;
	bool bLegacyRejected = false, bBackendNode = false;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM4WorldSnapshot Original;
		Original.Domain.bGlobalErosion = false;
		Original.Seed = 91234;
		Original.BaseHeightQ8 = 321;
		Original.OverhangNodeId = FGuid(5, 6, 7, 8);
		Original.HeightDetail.bEnabled = true;
		Original.HeightDetail.AmplitudeQ8 = 500;
		Original.HorizontalRadius = 2;
		Original.MaterialSetupPath = FSoftObjectPath(TEXT("/Game/Test/Material.Material"));
		FVoxelLandGraphNodeRecord Constant, Output;
		Constant.Id = FGuid(1, 1, 1, 1);
		Constant.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::FloatConstant);
		Constant.Pins.SetNum(2);
		Constant.Pins[0].Id = FGuid(2, 1, 1, 1);
		Constant.Pins[1].Id = FGuid(2, 1, 1, 2);
		Constant.Pins[1].bOutput = true;
		Output.Id = FGuid(1, 1, 1, 2);
		Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
		Output.Pins.SetNum(1);
		Output.Pins[0].Id = FGuid(2, 1, 1, 3);
		FVoxelLandGraphLinkRecord Link;
		Link.SourceNode = Constant.Id;
		Link.SourcePin = Constant.Pins[1].Id;
		Link.TargetNode = Output.Id;
		Link.TargetPin = Output.Pins[0].Id;
		Original.AuthoredHeightOutputId = FGuid(3, 1, 1, 1);
		Original.AuthoredHeightSource.Nodes = {Constant, Output};
		Original.AuthoredHeightSource.Links = {Link};
		Original.HeightScaleCm = 2750;
		FVoxelLandM4SavedWorld Saved;
		bCaptured = VoxelLandM4SavedWorld::Capture(Original, Saved, Error);
		if (!bCaptured)
		{
			return;
		}
		FVoxelLandM4SavedWorld Protected = Saved;
		FVoxelLandM4WorldSnapshot InvalidSnapshot = Original;
		InvalidSnapshot.HorizontalRadius = 0;
		bCaptureRadius =
			!VoxelLandM4SavedWorld::Capture(InvalidSnapshot, Protected, Error) && Error.Contains(TEXT("admission"));
		bCaptureAtomic = Protected.Snapshot.Seed == Saved.Snapshot.Seed
					  && Protected.Identity.CanonicalBytes == Saved.Identity.CanonicalBytes;
		InvalidSnapshot = Original;
		InvalidSnapshot.AuthoredHeightSource.Functions.SetNum(17);
		bCaptureOversized = !VoxelLandM4SavedWorld::Capture(InvalidSnapshot, Protected, Error)
						 && Error.Contains(TEXT("admission"))
						 && Protected.Identity.CanonicalBytes == Saved.Identity.CanonicalBytes;
		TArray<uint8> Bytes;
		FMemoryWriter MemoryWriter(Bytes);
		FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
		FVoxelLandM4SavedWorld::StaticStruct()->SerializeItem(Writer, &Saved, nullptr);
		FMemoryReader MemoryReader(Bytes);
		FObjectAndNameAsStringProxyArchive Reader(MemoryReader, true);
		FVoxelLandM4SavedWorld Loaded;
		FVoxelLandM4SavedWorld::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
		FVoxelLandM4WorldSnapshot Restored;
		bRestored = !Writer.IsError() && !Reader.IsError() && VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error);
		if (!bRestored)
		{
			return;
		}
		bFields = Restored.Seed == Original.Seed && Restored.BaseHeightQ8 == Original.BaseHeightQ8
			   && Restored.HeightDetail.AmplitudeQ8 == 500 && Restored.HorizontalRadius == 2
			   && Restored.MaterialSetupPath == Original.MaterialSetupPath
			   && Restored.AuthoredHeightSource.Nodes.Num() == 2 && Restored.AuthoredHeightSource.Links.Num() == 1
			   && Restored.AuthoredHeightOutputId == Original.AuthoredHeightOutputId
			   && Restored.HeightScaleCm == Original.HeightScaleCm
			   && Saved.Identity.FormatVersion == VoxelLandM4WorldIdentity::AuthoredHeightVersion
			   && Loaded.FormatVersion == FVoxelLandM4SavedWorld::AuthoredFormatVersion;
		bIdentity = VoxelLandM4WorldIdentity::Matches(Saved.Identity, Restored, Error);
		auto Changed = [&](const FVoxelLandM4WorldSnapshot& Snapshot) {
			FVoxelLandM4IdentityRecord Identity;
			return VoxelLandM4WorldIdentity::Capture(Snapshot, Identity, Error)
				&& Identity.CanonicalBytes != Saved.Identity.CanonicalBytes;
		};
		FVoxelLandM4WorldSnapshot Variant = Original;
		++Variant.AuthoredHeightOutputId.D;
		bOutputIdentity = Changed(Variant);
		Variant = Original;
		Variant.AuthoredHeightSource.Nodes[0].Pins[0].FloatDefault = 0.5f;
		bCompiledIdentity = Changed(Variant);
		Variant = Original;
		++Variant.HeightScaleCm;
		bScaleIdentity = Changed(Variant);
		Variant = Original;
		++Variant.BaseHeightQ8;
		bBaseIdentity = Changed(Variant);
		Variant = Original;
		Variant.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		Variant.AuthoredHeightSource.Nodes[0].Pins[0].FloatDefault = 65.f;
		bBackendNode = !VoxelLandM4Preparation::ValidateAuthoredHeight(Variant, Error)
					&& Error.Contains(TEXT("fixed-lattice")) && Error.Contains(Constant.Id.ToString());

		FVoxelLandM4SavedWorld Legacy;
		Legacy.Snapshot.Seed = 19;
		bLegacyRejected = !VoxelLandM4SavedWorld::Restore(Legacy, Restored, Error)
					   && Error.Contains(TEXT("reauthoring")) && Restored.Seed == Original.Seed;
		Loaded.Snapshot.Seed++;
		Restored.Seed = 777;
		bTampered = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error);
		bAtomic = Restored.Seed == 777;
		Loaded.Snapshot.Seed--;
		Loaded.FormatVersion++;
		bUnknown = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error) && Restored.Seed == 777;
		Loaded.FormatVersion--;
		Loaded.Snapshot.AuthoredHeightSource.Links.Reset();
		bBadProgram = !VoxelLandM4SavedWorld::Restore(Loaded, Restored, Error) && Restored.Seed == 777;
	}).Get();
	TestTrue(FString::Printf(TEXT("world captured: %s"), *Error), bCaptured);
	TestTrue(TEXT("world restored after property serialization"), bRestored);
	TestTrue(TEXT("capture rejects invalid startup radius"), bCaptureRadius);
	TestTrue(TEXT("capture rejects excessive nested metadata"), bCaptureOversized);
	TestTrue(TEXT("failed capture leaves its prior output unchanged"), bCaptureAtomic);
	TestTrue(TEXT("settings, projected graph and output binding survive"), bFields);
	TestTrue(TEXT("restored density identity matches"), bIdentity);
	TestTrue(TEXT("stable output ID changes authored identity"), bOutputIdentity);
	TestTrue(TEXT("compiled program changes authored identity"), bCompiledIdentity);
	TestTrue(TEXT("height scale changes authored identity"), bScaleIdentity);
	TestTrue(TEXT("base elevation changes authored identity"), bBaseIdentity);
	TestTrue(TEXT("legacy saves require reauthoring and preserve output"), bLegacyRejected);
	TestTrue(TEXT("fixed backend diagnostic identifies the authored node"), bBackendNode);
	TestTrue(TEXT("tampered density setting rejected"), bTampered);
	TestTrue(TEXT("unsupported version rejected"), bUnknown);
	TestTrue(TEXT("failed restore leaves output unchanged"), bAtomic);
	TestTrue(TEXT("invalid projected program rejected"), bBadProgram);
	return true;
}
#endif
