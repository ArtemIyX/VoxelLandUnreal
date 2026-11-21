#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/VoxelLandM4StageFingerprint.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4StageFingerprintFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM4StageNode M4FingerprintNode(uint32 Id, EVoxelLandM4StageKind Kind)
{
	FVoxelLandM4StageNode Node;
	Node.Id = FGuid(Id, 0, 0, 0);
	Node.Stage = Kind;
	return Node;
}

FVoxelLandM4StageFingerprintNode M4FingerprintSemantics(const FGuid& Id, uint8 Value)
{
	FVoxelLandM4StageFingerprintNode Result;
	Result.Id = Id;
	Result.SemanticsVersion = 1;
	Result.SemanticBytes = {Value};
	return Result;
}

FVoxelLandM4StageOutput M4FingerprintOutput(const TCHAR* Name, const FGuid& Id)
{
	FVoxelLandM4StageOutput Output;
	Output.Name = Name;
	Output.Producer = Id;
	return Output;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4StageFingerprintBytesTest, "VoxelLand.M4.StageFingerprint.CanonicalBytes", M4StageFingerprintFlags)
bool FVoxelLandM4StageFingerprintBytesTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4StageNode Node;
	Node.Id = FGuid(0x01020304, 0x11223344, 0x55667788, 0x99aabbcc);
	const TArray<FVoxelLandM4StageNode> Nodes = {Node};
	const TArray<FVoxelLandM4StageOutput> Outputs = {M4FingerprintOutput(TEXT("height"), Node.Id)};
	FVoxelLandM4StageFingerprintNode Sem = M4FingerprintSemantics(Node.Id, 0xaa);
	Sem.SemanticsVersion = 0x01020304;
	Sem.SemanticBytes.Add(0xbb);
	const TArray<FVoxelLandM4StageFingerprintNode> Semantics = {Sem};
	const TArray<uint8> Context = {0x7f};
	TArray<uint8> Bytes;
	uint64 Hash = 0;
	const FVoxelLandError Error =
		VoxelLandM4StageFingerprint::Build(Nodes, Outputs, TEXT("height"), Semantics, Context, Bytes, Hash);
	TestFalse(TEXT("single-node fingerprint succeeds"), bool(Error));
	// Independent literal: magic, format 1, output GUID A/B/C/D as LE uint32,
	// context length/data, node count, node GUID, stage, uses-Z, semantics version,
	// semantics length/data, and dependency count.
	const TArray<uint8> Expected = {'V', 'L', 'S', 'F', 1, 0, 0, 0, 4, 3, 2, 1, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77,
		0x66, 0x55, 0xcc, 0xbb, 0xaa, 0x99, 1, 0, 0, 0, 0x7f, 1, 0, 0, 0, 4, 3, 2, 1, 0x44, 0x33, 0x22, 0x11, 0x88,
		0x77, 0x66, 0x55, 0xcc, 0xbb, 0xaa, 0x99, 0, 0, 4, 3, 2, 1, 2, 0, 0, 0, 0xaa, 0xbb, 0, 0, 0, 0};
	TestTrue(TEXT("GUID and fixed-width values encode in defined byte order"), Bytes == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4StageFingerprintDependenciesTest, "VoxelLand.M4.StageFingerprint.Dependencies", M4StageFingerprintFlags)
bool FVoxelLandM4StageFingerprintDependenciesTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4StageNode A = M4FingerprintNode(1, EVoxelLandM4StageKind::Global);
	FVoxelLandM4StageNode B = M4FingerprintNode(2, EVoxelLandM4StageKind::Global);
	FVoxelLandM4StageNode C = M4FingerprintNode(3, EVoxelLandM4StageKind::Global);
	C.Dependencies = {B.Id, A.Id};
	FVoxelLandM4StageNode Detail = M4FingerprintNode(4, EVoxelLandM4StageKind::Sample);
	Detail.Dependencies = {C.Id};
	TArray<FVoxelLandM4StageNode> Nodes = {C, Detail, A, B};
	TArray<FVoxelLandM4StageFingerprintNode> Semantics = {M4FingerprintSemantics(A.Id, 1),
		M4FingerprintSemantics(B.Id, 2), M4FingerprintSemantics(C.Id, 3), M4FingerprintSemantics(Detail.Id, 4)};
	const TArray<FVoxelLandM4StageOutput> Outputs = {M4FingerprintOutput(TEXT("height"), C.Id)};
	TArray<uint8> Context = {42};
	TArray<uint8> Original;
	uint64 OriginalHash = 0;
	FVoxelLandError Error =
		VoxelLandM4StageFingerprint::Build(Nodes, Outputs, TEXT("height"), Semantics, Context, Original, OriginalHash);
	TestFalse(TEXT("initial graph succeeds"), bool(Error));
	Nodes.Swap(0, 3);
	Nodes[3].Dependencies.Swap(0, 1);
	Semantics.Swap(0, 2);
	TArray<uint8> Reordered;
	uint64 ReorderedHash = 0;
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestFalse(TEXT("reordered graph succeeds"), bool(Error));
	TestTrue(TEXT("node and dependency ordering do not change bytes"), Original == Reordered);
	TestEqual(TEXT("node and dependency ordering do not change hash"), OriginalHash, ReorderedHash);
	Semantics[3].SemanticBytes[0] = 99;
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestFalse(TEXT("downstream edit succeeds"), bool(Error));
	TestTrue(TEXT("downstream semantics do not change upstream bytes"), Original == Reordered);
	Semantics[0].SemanticBytes[0] = 77; // C after the swap
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestFalse(TEXT("upstream edit succeeds"), bool(Error));
	TestTrue(TEXT("upstream semantic edit changes bytes"), Original != Reordered);
	Semantics[0].SemanticBytes[0] = 3;
	Context[0] = 43;
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestFalse(TEXT("context edit succeeds"), bool(Error));
	TestTrue(TEXT("context edit changes bytes"), Original != Reordered);

	const TArray<uint8> Sentinel = {9, 8};
	Reordered = Sentinel;
	ReorderedHash = 123;
	Semantics[0].SemanticsVersion = 0;
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestTrue(TEXT("invalid version rejected"), bool(Error));
	TestTrue(TEXT("failure retains old bytes"), Reordered == Sentinel);
	TestEqual(TEXT("failure retains old hash"), ReorderedHash, uint64(123));
	Semantics[0].SemanticsVersion = 1;
	const FVoxelLandM4StageFingerprintNode DuplicateSemantic = Semantics[0];
	Semantics.Add(DuplicateSemantic);
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestTrue(TEXT("duplicate semantic records rejected"), bool(Error));
	Semantics.Pop();
	Semantics.RemoveAt(0);
	Error = VoxelLandM4StageFingerprint::Build(
		Nodes, Outputs, TEXT("height"), Semantics, Context, Reordered, ReorderedHash);
	TestTrue(TEXT("missing upstream semantics rejected"), bool(Error));
	return true;
}

#endif
