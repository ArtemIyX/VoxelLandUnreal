#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandM5SourceFingerprint.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SourceFingerprintTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SourceFingerprintVectorsTest,
	"VoxelLand.M5.Identity.SourceFingerprint.Vectors", M5SourceFingerprintTestFlags)
bool FVoxelLandM5SourceFingerprintVectorsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 1000000, Budget)))
	{
		return false;
	}
	FVoxelLandGraphSnapshot Graph;
	uint64 Result = 0;
	auto NeverCancel = [] {
		return false;
	};
	TestEqual(TEXT("empty graph succeeds"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	// Independent xxHash64 of the 20-byte version/source/count stream.
	TestEqual(TEXT("empty source vector"), Result, uint64(0xfe4f399ebe726e73ULL));
	TestEqual(TEXT("legacy source unchanged"), Result, FVoxelLandProgramCodec::SourceFingerprint(Graph));
	FVoxelLandGraphParameter Parameter;
	Parameter.Id = FGuid(1, 2, 3, 4);
	Parameter.Name = TEXT("A");
	Graph.Parameters.Add(Parameter);
	TestEqual(TEXT("parameter succeeds"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	// Independently encoded 68-byte little-endian fixture, including lowercase
	// ASCII name and the default graph parameter limits.
	TestEqual(TEXT("parameter source vector"), Result, uint64(0xd368c79780f36f3aULL));
	TestEqual(TEXT("legacy parameter fingerprint"), Result, FVoxelLandProgramCodec::SourceFingerprint(Graph));
	Graph.Parameters[0].FloatDefault = -0.0f;
	TestEqual(TEXT("signed zero succeeds"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("signed zero canonicalizes"), Result, uint64(0xd368c79780f36f3aULL));

	// Use existing codec only as compatibility evidence; the fixed vectors above
	// remain an independent oracle for the canonical byte order.
	FVoxelLandGraphNodeRecord Node;
	Node.Id = FGuid(9, 0, 0, 0);
	Node.OperatorId = FGuid(7, 1, 2, 3);
	Node.Configuration = {0, 1, 255};
	FVoxelLandGraphPinRecord Pin;
	Pin.Id = FGuid(8, 0, 0, 0);
	Pin.FloatDefault = 2.5f;
	Node.Pins.Add(Pin);
	Graph.Nodes.Add(Node);
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = Node.Id;
	Link.SourcePin = Pin.Id;
	Link.TargetNode = FGuid(10, 0, 0, 0);
	Link.TargetPin = FGuid(11, 0, 0, 0);
	Graph.Links.Add(Link);
	TestEqual(TEXT("records succeed"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	// Independently encoded 227-byte node/configuration/pin/link stream and
	// calculated with a standalone xxHash64 reference.
	TestEqual(TEXT("mixed source vector"), Result, uint64(0xc7f93c6f217d113cULL));
	TestEqual(TEXT("legacy mixed records fingerprint"), Result, FVoxelLandProgramCodec::SourceFingerprint(Graph));
	FVoxelLandGraphNodeRecord OtherNode = Node;
	OtherNode.Id = FGuid(2, 0, 0, 0);
	OtherNode.Configuration = {19};
	Graph.Nodes.Add(OtherNode);
	FVoxelLandGraphLinkRecord OtherLink = Link;
	OtherLink.TargetNode = FGuid(1, 0, 0, 0);
	Graph.Links.Add(OtherLink);
	TestEqual(TEXT("two records succeed"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	const uint64 OrderedResult = Result;
	Swap(Graph.Nodes[0], Graph.Nodes[1]);
	Swap(Graph.Links[0], Graph.Links[1]);
	TestEqual(TEXT("reordered records succeed"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("record order independent"), Result, OrderedResult);
	TestEqual(TEXT("legacy reordered fingerprint"), Result, FVoxelLandProgramCodec::SourceFingerprint(Graph));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SourceFingerprintFailureTest,
	"VoxelLand.M5.Identity.SourceFingerprint.Failures", M5SourceFingerprintTestFlags)
bool FVoxelLandM5SourceFingerprintFailureTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(8 * 1024 * 1024, 1000000, Budget)))
	{
		return false;
	}
	constexpr uint64 Sentinel = 0x9123456789abcdefULL;
	uint64 Result = Sentinel;
	FVoxelLandGraphSnapshot Graph;
	FVoxelLandGraphParameter P;
	P.Id = FGuid(1, 0, 0, 0);
	P.Name = TEXT("name");
	Graph.Parameters = {P, P};
	auto NeverCancel = [] {
		return false;
	};
	TestEqual(TEXT("duplicate rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("duplicate retains output"), Result, Sentinel);
	Graph.Parameters.RemoveAt(1);
	Graph.Parameters[0].Name = TEXT("\u00e9");
	TestEqual(TEXT("non ASCII rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("non ASCII retains output"), Result, Sentinel);
	Graph.Parameters.Empty();
	FVoxelLandGraphNodeRecord FunctionNode;
	FunctionNode.FunctionPath = FSoftObjectPath(TEXT("/Game/Graph.Graph"));
	Graph.Nodes.Add(FunctionNode);
	TestEqual(TEXT("function path rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("function retains output"), Result, Sentinel);
	Graph.Nodes.Empty();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoWork;
	FVoxelLandM5Budget::TryCreate(8192, 0, NoWork);
	TestEqual(TEXT("work rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, NoWork.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("work retains output"), Result, Sentinel);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoMemory;
	FVoxelLandM5Budget::TryCreate(0, 1000, NoMemory);
	TestEqual(TEXT("memory rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, NoMemory.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("memory retains output"), Result, Sentinel);
	TestEqual(TEXT("cancellation rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(
			Graph, Budget.ToSharedRef(), [] { return true; }, Result)
				.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancellation retains output"), Result, Sentinel);

	// Empty graph: 1 unit preflight, then 20 encoded bytes + 64 units fixed
	// work; payload reservation is 20 bytes plus 4096 bytes scratch allowance.
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Exact;
	FVoxelLandM5Budget::TryCreate(4116, 85, Exact);
	TestEqual(TEXT("exact empty limits accepted"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Exact.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::None));
	Result = Sentinel;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OneLessWork;
	FVoxelLandM5Budget::TryCreate(4116, 84, OneLessWork);
	TestEqual(TEXT("one below work rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, OneLessWork.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("work boundary retains output"), Result, Sentinel);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OneLessMemory;
	FVoxelLandM5Budget::TryCreate(4115, 85, OneLessMemory);
	TestEqual(TEXT("one below memory rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, OneLessMemory.ToSharedRef(), NeverCancel, Result).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("memory boundary retains output"), Result, Sentinel);

	Graph.Parameters.Add(P);
	Graph.Nodes.AddDefaulted();
	Graph.Nodes[0].Id = FGuid(2, 0, 0, 0);
	Graph.Links.AddDefaulted();
	int32 Polls = 0;
	const auto CancelDuringWrite = [&Polls] {
		return ++Polls == 8;
	};
	TestEqual(TEXT("cancellation after sorting rejected"),
		int32(VoxelLand::M5::SourceFingerprint::Compute(Graph, Budget.ToSharedRef(), CancelDuringWrite, Result).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("partial cancellation retains output"), Result, Sentinel);
	TestEqual(TEXT("partial cancellation reached writer"), Polls, 8);
	int64 LiveBytes = -1, PeakBytes = -1, Work = -1;
	Budget->GetUsage(LiveBytes, PeakBytes, Work);
	TestEqual(TEXT("partial cancellation releases scratch"), LiveBytes, int64(0));
	return true;
}
#endif
