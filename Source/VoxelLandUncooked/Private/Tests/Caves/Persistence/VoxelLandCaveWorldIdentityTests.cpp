#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Terrain/Persistence/VoxelLandTerrainIdentityRecord.h"
#include "Data/Caves/Persistence/VoxelLandCaveIdentityRecord.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Serialization/VoxelLandProgramCodec.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Persistence/VoxelLandWorldIdentity.h"
#include "World/Caves/Persistence/VoxelLandCaveWorldIdentity.h"

namespace
{
constexpr EAutomationTestFlags M5IdentityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M5IdentityMakeBase(FVoxelLandM4WorldSnapshot& S, FString& Error)
{
	S.Domain.bGlobalErosion = false;
	S.OverhangNodeId = FGuid(1, 2, 3, 4);
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
	FVoxelLandM4ControlSnapshot* Channels[] = {&S.Continental, &S.Mountain, &S.Peaks};
	for (FVoxelLandM4ControlSnapshot* C : Channels)
	{
		C->Source.Nodes = {Constant, Output};
		C->Source.Links = {Link};
		FVoxelLandProgramDraft Draft;
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (!FVoxelLandGraphCompiler::Compile(C->Source, Draft, Error) || FVoxelLandProgram::Validate(Draft, Program)
			|| !FVoxelLandProgramCodec::Pack(C->Source, *Program, C->Payload, Error))
		{
			return false;
		}
		FVoxelLandM4CurveAuthoringKey First, Last;
		Last.InputQ16 = 65535;
		C->CurveKeys = {First, Last};
		C->CookedCurve.SourceKeys = C->CurveKeys;
		C->CookedCurve.BakeVersion = 1;
		C->CookedCurve.SamplesQ8.Init(0, 4097);
		FCanonicalWriter W;
		for (int32 Value : C->CookedCurve.SamplesQ8)
		{
			W.U32(uint32(Value));
		}
		const uint64 Hash = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
		FMemory::Memcpy(&C->CookedCurve.SamplesHash, &Hash, sizeof(Hash));
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5WorldIdentityTest, "VoxelLand.M5.Identity.CanonicalAndFailures", M5IdentityTestFlags)

bool FVoxelLandM5WorldIdentityTest::RunTest(const FString& Parameters)
{
	bool bFixture = false, bCapture = false, bMatch = false, bBaseIncluded = false, bAllFields = true;
	bool bBadVersion = false, bCorrupt = false, bAtomic = false, bDisabled = false, bPocketOnly = false;
	bool bUnsupportedMask = false, bUnsupportedWater = false, bInvalidBounds = false, bBaseRevalidated = false;
	bool bDryIdentity = false, bDryMismatchAtomic = false, bV2IgnoreAtomic = false;
	bool bBadCandidateLimit = false, bBadAttempts = false, bBadRadii = false, bBadPeriod = false;
	bool bBadAmplitudeNegative = false, bBadAmplitudeHigh = false;
	bool bRegionalCapture = false, bBadAlignment = false, bBadRegionVoxelMultiple = false, bZeroVoxel = false;
	bool bInvalidRouteLimits = true;
	FString Error;
	Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandM5WorldSnapshot S;
		bFixture = M5IdentityMakeBase(S.Base, Error);
		if (!bFixture)
		{
			return;
		}
		S.Caves.bEnabled = true;
		S.Caves.StageId = FGuid(11, 22, 33, 44);
		S.Caves.Bounds.MinInclusive.X = S.Caves.Bounds.MinInclusive.Y = S.Caves.Bounds.MinInclusive.Z = -10000;
		S.Caves.Bounds.MaxExclusive.X = S.Caves.Bounds.MaxExclusive.Y = S.Caves.Bounds.MaxExclusive.Z = 10000;
		FVoxelLandM5IdentityRecord Identity;
		FVoxelLandM4IdentityRecord Base;
		bCapture = VoxelLandM5WorldIdentity::Capture(S, Identity, Error)
				&& VoxelLandM4WorldIdentity::Capture(S.Base, Base, Error);
		if (!bCapture)
		{
			return;
		}
		bMatch = VoxelLandM5WorldIdentity::Matches(Identity, S, Error);
		// M5 prefix and the independently captured full M4 identity at offset 44:
		// magic 4 + eight version words 32 + base version 4 + length 4.
		bBaseIncluded = Identity.CanonicalBytes.Num() >= 44 + Base.CanonicalBytes.Num()
					 && Identity.CanonicalBytes[0] == 'V' && Identity.CanonicalBytes[1] == 'L'
					 && Identity.CanonicalBytes[2] == 'M' && Identity.CanonicalBytes[3] == '5'
					 && FMemory::Memcmp(Identity.CanonicalBytes.GetData() + 44, Base.CanonicalBytes.GetData(),
							Base.CanonicalBytes.Num())
							== 0;
		auto Changed = [&](const FVoxelLandM5WorldSnapshot& Other) {
			FVoxelLandM5IdentityRecord ChangedIdentity;
			return VoxelLandM5WorldIdentity::Capture(Other, ChangedIdentity, Error)
				&& ChangedIdentity.CanonicalBytes != Identity.CanonicalBytes
				&& !VoxelLandM5WorldIdentity::Matches(Identity, Other, Error);
		};
		FVoxelLandM5WorldSnapshot X = S;
		++X.Base.Seed;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Salt;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.StageId.A;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Bounds.MinInclusive.X;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.RegionSizeCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.RegionOriginXCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.RegionOriginYCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.RoofThicknessCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Capsule.StandingHeightCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Capsule.RadiusCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Capsule.MaxSlopeDegrees;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Capsule.MaxStepCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Capsule.ClearanceMarginCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MinChambers;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MaxChambers;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.EntranceCount;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MinLoops;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MaxLoops;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MinTunnelRadiusCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MaxTunnelRadiusCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MinChamberRadiusCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Topology.MaxChamberRadiusCm;
		bAllFields &= Changed(X);
		X = S;
		X.Caves.Pockets.bEnabled = true;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Pockets.LatticeSpacingCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Pockets.ThresholdQ16;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Pockets.AmplitudeCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Pockets.MinDepthCm;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Pockets.MaxDepthCm;
		bAllFields &= Changed(X);
		X = S;
		X.Caves.Pockets.bAllowAttachment = false;
		bAllFields &= Changed(X);
		X = S;
		X.Caves.Pockets.bAllowEnclosedVoids = false;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Pockets.Salt;
		bAllFields &= Changed(X);
		X = S;
		X.Caves.Protection.bProtectAll = true;
		bAllFields &= Changed(X);
		X = S;
		++X.Caves.Water.LevelCm;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxCandidates;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxEdges;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxSegments;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxJunctions;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxRoutes;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxRouteLegs;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxRouteFeatureRefs;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxAttempts;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxBytes;
		bAllFields &= Changed(X);
		X = S;
		--X.Caves.Limits.MaxValidationWork;
		bAllFields &= Changed(X);
		X = S;
		X.Manifest.Geometry = 3;
		FVoxelLandM5IdentityRecord Retained = Identity;
		bBadVersion = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		bAtomic =
			Retained.CanonicalBytes == Identity.CanonicalBytes && Retained.FingerprintBits == Identity.FingerprintBits;
		FVoxelLandM5IdentityRecord Tampered = Identity;
		Tampered.CanonicalBytes[0] ^= 1;
		bCorrupt = !VoxelLandM5WorldIdentity::Matches(Tampered, S, Error);
		X = S;
		X.Caves.bEnabled = false;
		bDisabled = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.Pockets.bEnabled = true;
		bPocketOnly = VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Protection.bRequireMask = true;
		bUnsupportedMask = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Water.Policy = EVoxelLandM5WaterPolicy::RejectFlooded;
		bUnsupportedWater = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		X.Manifest.Descriptor = 2;
		FVoxelLandM5IdentityRecord Dry;
		bDryIdentity = VoxelLandM5WorldIdentity::Capture(X, Dry, Error) && Dry.CanonicalBytes != Identity.CanonicalBytes
					&& VoxelLandM5WorldIdentity::Matches(Dry, X, Error);
		Retained = Identity;
		X.Manifest.Descriptor = 1;
		bDryMismatchAtomic = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error)
						  && Retained.CanonicalBytes == Identity.CanonicalBytes
						  && Retained.FingerprintBits == Identity.FingerprintBits;
		X = S;
		X.Manifest.Descriptor = 2;
		bV2IgnoreAtomic = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error)
					   && Retained.CanonicalBytes == Identity.CanonicalBytes
					   && Retained.FingerprintBits == Identity.FingerprintBits;
		X = S;
		X.Caves.Bounds.MaxExclusive.X = X.Caves.Bounds.MinInclusive.X;
		bInvalidBounds = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Limits.MaxCandidates = X.Caves.Topology.MaxChambers - 1;
		bBadCandidateLimit = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Limits.MaxJunctions = 0;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.Limits.MaxJunctions = 8193;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Limits.MaxRoutes = 0;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.Limits.MaxRoutes = 16385;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Limits.MaxRouteLegs = 0;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.Limits.MaxRouteLegs = 65537;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Limits.MaxRouteFeatureRefs = 0;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.Limits.MaxRouteFeatureRefs = 131073;
		bInvalidRouteLimits &= !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Limits.MaxAttempts = 65;
		bBadAttempts = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Topology.MinChamberRadiusCm = X.Caves.Topology.MaxTunnelRadiusCm - 1;
		bBadRadii = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Pockets.LatticeSpacingCm = 1048577;
		bBadPeriod = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Caves.Pockets.AmplitudeCm = -1;
		bBadAmplitudeNegative = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.Pockets.AmplitudeCm = 1048577;
		bBadAmplitudeHigh = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Base.Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
		X.Caves.PlanningMode = EVoxelLandM5PlanningMode::Regional;
		X.Caves.RegionSizeCm = 10000;
		bRegionalCapture = VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		++X.Caves.Bounds.MinInclusive.X;
		bBadAlignment = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.RegionSizeCm = 10001;
		X.Caves.Bounds.MinInclusive.X = X.Caves.Bounds.MinInclusive.Y = -10001;
		X.Caves.Bounds.MaxExclusive.X = X.Caves.Bounds.MaxExclusive.Y = 10001;
		bBadRegionVoxelMultiple = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X.Caves.RegionSizeCm = 10000;
		X.Base.VoxelSizeCm = 0;
		bZeroVoxel = !VoxelLandM5WorldIdentity::Capture(X, Retained, Error);
		X = S;
		X.Base.Continental.CookedCurve.SamplesQ8[0] ^= 1;
		bBaseRevalidated = !VoxelLandM5WorldIdentity::Matches(Identity, X, Error);
	}).Wait();
	TestTrue(TEXT("M4 fixture compiled"), bFixture);
	TestTrue(TEXT("M5 capture succeeds"), bCapture);
	TestTrue(TEXT("M5 identity matches"), bMatch);
	TestTrue(TEXT("canonical bytes contain the complete M4 identity"), bBaseIncluded);
	TestTrue(TEXT("semantic settings affect identity"), bAllFields);
	TestTrue(TEXT("unsupported version rejected"), bBadVersion);
	TestTrue(TEXT("failure preserves output"), bAtomic);
	TestTrue(TEXT("corrupt identity rejected"), bCorrupt);
	TestTrue(TEXT("disabled stages reject M5 capture"), bDisabled);
	TestTrue(TEXT("pocket-only settings select M5 identity"), bPocketOnly);
	TestTrue(TEXT("missing protection mask rejected"), bUnsupportedMask);
	TestTrue(TEXT("unsupported water policy rejected"), bUnsupportedWater);
	TestTrue(TEXT("dry identity is distinct and matches"), bDryIdentity);
	TestTrue(TEXT("dry v1 mismatch preserves identity"), bDryMismatchAtomic);
	TestTrue(TEXT("ignore v2 mismatch preserves identity"), bV2IgnoreAtomic);
	TestTrue(TEXT("invalid bounds rejected"), bInvalidBounds);
	TestTrue(TEXT("candidate limit below chamber count rejected"), bBadCandidateLimit);
	TestTrue(TEXT("route and junction resource caps enforce both bounds"), bInvalidRouteLimits);
	TestTrue(TEXT("planner attempt limit enforced"), bBadAttempts);
	TestTrue(TEXT("tunnel cannot exceed chamber radius"), bBadRadii);
	TestTrue(TEXT("pocket period respects sampler limit"), bBadPeriod);
	TestTrue(TEXT("negative pocket amplitude rejected"), bBadAmplitudeNegative);
	TestTrue(TEXT("pocket amplitude above limit rejected"), bBadAmplitudeHigh);
	TestTrue(TEXT("aligned regional inputs accepted"), bRegionalCapture);
	TestTrue(TEXT("unaligned regional bounds rejected"), bBadAlignment);
	TestTrue(TEXT("region size must be a voxel multiple"), bBadRegionVoxelMultiple);
	TestTrue(TEXT("zero voxel size rejected without division"), bZeroVoxel);
	TestTrue(TEXT("base program inputs are revalidated"), bBaseRevalidated);
	return true;
}
#endif