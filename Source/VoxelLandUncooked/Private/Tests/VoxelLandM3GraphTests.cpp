#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandM1Fixtures.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM1SampleResult.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "FastNoiseNative.h"
#include "Async/Async.h"
#include "Algo/Reverse.h"
#include "Operators/VoxelLandSquareOperator.h"
#include "Operators/VoxelLandExampleFilterOperator.h"
#include "Operators/VoxelLandOperator.h"
#include <cfloat>
#include "Misc/AutomationTest.h"
#include "Generation/VoxelLandPower.h"
#include <cmath>
#include <limits>

namespace
{
constexpr EAutomationTestFlags M3GraphFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
using namespace VoxelLandM1Fixtures;
using VoxelLandM1TestData::FSampleResult;

TAtomic<int32> MismatchedSimdCalls(0);
TAtomic<bool> ShortSimdOutput(false);
TAtomic<int32> ShortProviderVirtualCalls(0);

class FMismatchedSimdWorker final : public IVoxelLandOperatorWorker
{
public:
    bool Evaluate(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count, FString& Error) override
    {
        if (Inputs.Num() != 1 || !Inputs[0] || Output.Num() != Count || Count < 1 || Count > 256)
        {
            Error = TEXT("Invalid mismatch-provider batch");
            return false;
        }
        const float* Input = static_cast<const float*>(Inputs[0]);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (!Output.WriteFloat(Index, Input[Index])) { return false; }
        }
        return true;
    }
    bool EvaluateSimd(TConstArrayView<const void*> Inputs, FVoxelLandOperatorOutput& Output, int32 Count, FString& Error) override
    {
        ++MismatchedSimdCalls;
        if (Inputs.Num() != 1 || !Inputs[0]) { return false; }
        const float* Input = static_cast<const float*>(Inputs[0]);
        for (int32 Index = 0; Index < Count - (ShortSimdOutput.Load() ? 1 : 0); ++Index)
        {
            if (!Output.WriteFloat(Index, Input[Index] + (!ShortSimdOutput.Load() && Index == 0 ? 1.f : 0.f))) { return false; }
        }
        return true;
    }
};

class FMismatchedSimdProvider final : public IVoxelLandOperator
{
public:
    FGuid Id() const override { return FGuid(0x18f06b17, 0xaa584ce3, 0x9cccfa22, 0x74cf8cba); }
    int32 Version() const override { return 1; }
    TArray<EVoxelLandGraphValueType> InputTypes() const override { return {EVoxelLandGraphValueType::Float}; }
    EVoxelLandGraphValueType OutputType() const override { return EVoxelLandGraphValueType::Float; }
    uint32 MaximumWorkerBytes() const override { return sizeof(FMismatchedSimdWorker); }
    EVoxelLandOperatorSimdRequirement SimdRequirement() const override { return EVoxelLandOperatorSimdRequirement::SSE2; }
    bool ValidateConfiguration(TConstArrayView<uint8> Config, FString& Error) const override { return Config.IsEmpty(); }
    TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8> Config, uint64 Seed, FString& Error) const override
    {
        return MakeUnique<FMismatchedSimdWorker>();
    }
};

class FShortWriteWorker final : public IVoxelLandOperatorWorker
{
public:
    explicit FShortWriteWorker(EVoxelLandGraphValueType InType) : Type(InType) {}
    bool Evaluate(TConstArrayView<const void*>, FVoxelLandOperatorOutput& Output, int32 Count, FString&) override
    {
        for (int32 Index = 0; Index < Count - 1; ++Index)
        {
            if (Type == EVoxelLandGraphValueType::Float) { Output.WriteFloat(Index, 1.f); }
            else if (Type == EVoxelLandGraphValueType::Int32) { Output.WriteInt(Index, MIN_int32); }
            else { Output.WriteBool(Index, 1); }
        }
        return true;
    }
    bool EvaluateFilter(TConstArrayView<int16>, int32, FVoxelLandOperatorOutput& Output, TFunctionRef<bool()>, FString&) override
    {
        const int32 WrittenCount = Output.Num() - (Output.Num() < 256 ? 1 : 0);
        for (int32 Index = 0; Index < WrittenCount; ++Index) { Output.WriteDensity(Index, MIN_int16); }
        return true;
    }
private:
    EVoxelLandGraphValueType Type;
};
class FShortWriteProvider final : public IVoxelLandOperator
{
public:
    FShortWriteProvider(EVoxelLandGraphValueType InType, EVoxelLandOperatorStage InStage) : Type(InType), OperatorStage(InStage) {}
    EVoxelLandOperatorStage Stage() const override { return OperatorStage; }
    FGuid Id() const override { return FGuid(0xf9033231, 0x823043a8, uint32(Type) + 1, uint32(OperatorStage) + 1); }
    int32 Version() const override { return 1; }
    TArray<EVoxelLandGraphValueType> InputTypes() const override { ++ShortProviderVirtualCalls; return {EVoxelLandGraphValueType::Float}; }
    EVoxelLandGraphValueType OutputType() const override { return Type; }
    uint32 MaximumWorkerBytes() const override { return sizeof(FShortWriteWorker); }
    bool ValidateConfiguration(TConstArrayView<uint8> Config, FString&) const override { return Config.IsEmpty(); }
    TUniquePtr<IVoxelLandOperatorWorker> CreateWorker(TConstArrayView<uint8>, uint64, FString&) const override { return MakeUnique<FShortWriteWorker>(Type); }
private:
    EVoxelLandGraphValueType Type;
    EVoxelLandOperatorStage OperatorStage;
};

FVoxelLandGraphNodeRecord Node(EVoxelLandBuiltinNode Kind, int32 Id, int32 Inputs, bool bOutput)
{
    FVoxelLandGraphNodeRecord Result;
    Result.Id = FGuid(0x1234, 1, 1, Id);
    Result.OperatorId = VoxelLandBuiltinNodeId(Kind);
    for (int32 Index = 0; Index < Inputs + int32(bOutput); ++Index)
    {
        FVoxelLandGraphPinRecord Pin;
        Pin.Id = FGuid(0x5678, 1, uint32(Id), uint32(Index + 1));
        Pin.bOutput = Index == Inputs && bOutput;
        Result.Pins.Add(Pin);
    }
    return Result;
}

void Link(FVoxelLandGraphSnapshot& Graph, int32 Source, int32 Target, int32 Input)
{
    FVoxelLandGraphLinkRecord Record;
    Record.SourceNode = Graph.Nodes[Source].Id;
    Record.SourcePin = Graph.Nodes[Source].Pins.Last().Id;
    Record.TargetNode = Graph.Nodes[Target].Id;
    Record.TargetPin = Graph.Nodes[Target].Pins[Input].Id;
    Graph.Links.Add(Record);
}

FVoxelLandGraphSnapshot SimpleGraph()
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 2, 1, true));
    Graph.Nodes[1].Pins[0].FloatDefault = 2.f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Multiply, 3, 2, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 4, 1, false));
    Link(Graph, 0, 2, 0);
    Link(Graph, 1, 2, 1);
    Link(Graph, 2, 3, 0);
    return Graph;
}

FSampleResult Sample(FVoxelLandProgramDraft Draft, EVoxelLandVMBackend Backend,
    FVoxelLandZoneProfile Profile, int32 Count, uint64 Seed = 314159)
{
    return Async(EAsyncExecution::ThreadPool, [Draft, Backend, Profile, Count, Seed] {
        FSampleResult Result;
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        Result.Error = FVoxelLandProgram::Validate(Draft, Program);
        if (Result.Error) { return Result; }
        FVoxelLandGeneratorSettings Settings;
        Settings.Program = Program;
        Settings.Profile = Profile;
        Settings.WorldSeed = Seed;
        Settings.Backends.VM = Backend;
        TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
        Result.Error = FVoxelLandGenerator::Create(Settings, Generator);
        if (Result.Error) { return Result; }
        TUniquePtr<FVoxelLandWorkerContext> Worker;
        Result.Error = Generator->CreateWorker(Worker);
        if (Result.Error) { return Result; }
        Result.VM = Worker->GetVMBackend();
        Result.NoiseBackend = Worker->GetNoiseBackend();
        TArray<FVector3f> Points;
        for (int32 I = 0; I < Count; ++I)
        {
            // Cover many native lattice cells, including both signs, at ordinary graph frequencies.
            Points.Emplace(float(I - 129) * 106.f, float((I * 31) % 257 - 128) * 56.f,
                float((I * 11) % 123 - 61) * 40.f);
        }
        Result.Error = Generator->EvaluateSamples(Points, *Worker, Result.Values);
        return Result;
    }).Get();
}

FVoxelLandProgramDraft TypedSelect()
{
    FVoxelLandProgramDraft Draft;
    Draft.FormatVersion = 2;
    Draft.RegisterCount = 5;
    Draft.Constants = {9.f};
    FVoxelLandParameterSlot Integer;
    Integer.Name = TEXT("amount");
    Integer.Type = EVoxelLandParameterType::Int32;
    Integer.IntDefault = 42;
    Draft.Parameters.Add(Integer);
    FVoxelLandParameterSlot Boolean;
    Boolean.Name = TEXT("enabled");
    Boolean.Type = EVoxelLandParameterType::Bool;
    Draft.Parameters.Add(Boolean);
    Draft.Instructions = {Op(EVoxelLandOpcode::LoadIntParameter, 0, 0, 0, 0, 0),
        Op(EVoxelLandOpcode::IntToFloat, 1, 0), Op(EVoxelLandOpcode::LoadBoolParameter, 2, 0, 0, 0, 1),
        Op(EVoxelLandOpcode::LoadConstant, 3, 0, 0, 0, 0), Op(EVoxelLandOpcode::SelectFloat, 4, 2, 1, 3),
        Op(EVoxelLandOpcode::StoreDensity, 0, 4)};
    return Draft;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3CompilerCodec, "VoxelLand.M3.CompilerAndSerialization", M3GraphFlags)
bool FVoxelLandM3CompilerCodec::RunTest(const FString& Parameters)
{
    const FVoxelLandGraphSnapshot Graph = SimpleGraph();
    FVoxelLandProgramDraft A;
    FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, A, Error))
    {
        AddError(FString::Printf(TEXT("Graph compile failed: %s"), *Error));
        return false;
    }
    FVoxelLandGraphSnapshot Reordered = Graph;
    Algo::Reverse(Reordered.Nodes);
    Algo::Reverse(Reordered.Links);
    Reordered.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionY, 99, 0, true));
    FVoxelLandProgramDraft B;
    TestTrue(TEXT("Reordered graph compiles"), FVoxelLandGraphCompiler::Compile(Reordered, B, Error));
    const bool bStable = Async(EAsyncExecution::ThreadPool, [A, B]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> PA, PB;
        return !FVoxelLandProgram::Validate(A, PA) && !FVoxelLandProgram::Validate(B, PB) &&
            PA->GetFingerprint() == PB->GetFingerprint();
    }).Get();
    TestTrue(TEXT("Node and link storage order and unreachable nodes do not alter program identity"), bStable);

	const bool bRoundTrip = Async(EAsyncExecution::ThreadPool, [Graph, A]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program, Loaded;
        FString Failure;
        FVoxelLandSerializedProgram Payload;
        if (FVoxelLandProgram::Validate(A, Program) || !FVoxelLandProgramCodec::Pack(Graph, *Program, Payload, Failure) ||
            !FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure) || Loaded->GetFingerprint() != Program->GetFingerprint())
        {
            return false;
        }
		FVoxelLandGraphSnapshot Changed = Graph;
		Changed.Nodes[1].Pins[0].FloatDefault = 3.f;
		if (FVoxelLandProgramCodec::Load(Changed, Payload, Loaded, Failure))
		{
			return false;
		}
		FVoxelLandSerializedProgram StaleSemantic = Payload;
		StaleSemantic.CompilerSemanticVersion = 0;
		if (FVoxelLandProgramCodec::Load(Graph, StaleSemantic, Loaded, Failure)
			|| !Failure.Contains(TEXT("semantic version")))
		{
			return false;
		}
		FVoxelLandSerializedProgram Obsolete = Payload;
		Obsolete.ProgramVersion = 1;
		if (FVoxelLandProgramCodec::Load(Graph, Obsolete, Loaded, Failure))
		{
			return false;
		}
		Payload.ProgramFingerprint ^= 1;
		return !FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure);
	}).Get();
	TestTrue(TEXT("Serialized program round-trips and rejects stale source and fingerprint corruption"), bRoundTrip);

	FVoxelLandGraphSnapshot Invalid = Graph;
    Invalid.Nodes[1].Id = Invalid.Nodes[0].Id;
    TestFalse(TEXT("Duplicate IDs rejected"), FVoxelLandGraphCompiler::Compile(Invalid, B, Error));
    Invalid = Graph;
    Invalid.Links[0].SourcePin = Invalid.Nodes[1].Pins[0].Id;
    TestFalse(TEXT("Bad pin ownership rejected"), FVoxelLandGraphCompiler::Compile(Invalid, B, Error));
    Invalid = Graph;
    Invalid.Nodes[2].Pins[0].Type = EVoxelLandGraphValueType::Bool;
    TestFalse(TEXT("Mismatched typed pins rejected"), FVoxelLandGraphCompiler::Compile(Invalid, B, Error));
    Invalid = Graph;
    Invalid.Links[0].SourceNode = Invalid.Nodes[2].Id;
    Invalid.Links[0].SourcePin = Invalid.Nodes[2].Pins.Last().Id;
    TestFalse(TEXT("Cycles rejected"), FVoxelLandGraphCompiler::Compile(Invalid, B, Error));
    Invalid = Graph;
    Invalid.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 100, 1, false));
    TestFalse(TEXT("Duplicate density output rejected"), FVoxelLandGraphCompiler::Compile(Invalid, B, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3CheckedOperatorOutput, "VoxelLand.M3.CheckedOperatorOutput", M3GraphFlags)
bool FVoxelLandM3CheckedOperatorOutput::RunTest(const FString& Parameters)
{
    FString Error;
    for (int32 Start = MIN_int16; Start <= MAX_int16; Start += 256)
    {
        int16 Values[256];
        FVoxelLandOperatorOutput Writer(MakeArrayView(Values, 256));
        for (int32 I = 0; I < 256; ++I) { Writer.WriteDensity(I, int16(Start + I)); }
        TestTrue(TEXT("Every int16 value can be written"), Writer.IsComplete(Error));
        TestEqual(TEXT("Density endpoints are preserved"), int32(Values[0]), Start);
    }
    int32 Integers[4] = {};
    FVoxelLandOperatorOutput IntWriter(EVoxelLandGraphValueType::Int32, Integers, 4);
    const int32 Ints[] = {MIN_int32, MAX_int32, 0, -1};
    TestTrue(TEXT("Int32 block accepts extremes"), IntWriter.WriteInt4(0, Ints) && IntWriter.IsComplete(Error));
    TestTrue(TEXT("Int32 values preserved"), FMemory::Memcmp(Ints, Integers, sizeof(Ints)) == 0);
    float Floats[4] = {};
    FVoxelLandOperatorOutput FloatWriter(EVoxelLandGraphValueType::Float, Floats, 4);
    const float FloatValues[] = {-FLT_MAX, FLT_MAX, 0.f, -0.f};
    TestTrue(TEXT("Finite float block accepts extremes and signed zero"), FloatWriter.WriteFloat4(0, FloatValues) && FloatWriter.IsComplete(Error));
    TestTrue(TEXT("Float bits preserved"), FMemory::Memcmp(Floats, FloatValues, sizeof(FloatValues)) == 0);
    int32 Booleans[2] = {};
    FVoxelLandOperatorOutput BoolWriter(EVoxelLandGraphValueType::Bool, Booleans, 2);
    TestTrue(TEXT("Both bool values accepted"), BoolWriter.WriteBool(0, 0) && BoolWriter.WriteBool(1, 1) && BoolWriter.IsComplete(Error));
    FVoxelLandOperatorOutput Invalid(EVoxelLandGraphValueType::Bool, Booleans, 2);
    TestFalse(TEXT("Invalid bool rejected"), Invalid.WriteBool(0, 2));
    TestFalse(TEXT("Invalid writer cannot complete"), Invalid.IsComplete(Error));
    FVoxelLandOperatorOutput Duplicate(EVoxelLandGraphValueType::Int32, Integers, 4);
    TestTrue(TEXT("Initial int write succeeds"), Duplicate.WriteInt(0, 1));
    TestFalse(TEXT("Duplicate write rejected"), Duplicate.WriteInt(0, 1));
    FVoxelLandOperatorOutput Bounds(EVoxelLandGraphValueType::Int32, Integers, 4);
    TestFalse(TEXT("Out-of-range write rejected"), Bounds.WriteInt(4, 1));
    FVoxelLandOperatorOutput BlockBounds(EVoxelLandGraphValueType::Int32, Integers, 4);
    TestFalse(TEXT("Overflowing block index rejected"), BlockBounds.WriteInt4(MAX_int32, Ints));
    FVoxelLandOperatorOutput WrongType(EVoxelLandGraphValueType::Int32, Integers, 4);
    TestFalse(TEXT("Wrong-type write rejected"), WrongType.WriteFloat(0, 1.f));
    FVoxelLandOperatorOutput NonFinite(EVoxelLandGraphValueType::Float, Floats, 4);
    TestFalse(TEXT("Nonfinite output rejected"), NonFinite.WriteFloat(0, std::numeric_limits<float>::infinity()));

    for (EVoxelLandGraphValueType Type : {EVoxelLandGraphValueType::Float, EVoxelLandGraphValueType::Int32, EVoxelLandGraphValueType::Bool})
    {
        TSharedRef<FShortWriteProvider, ESPMode::ThreadSafe> Provider = MakeShared<FShortWriteProvider, ESPMode::ThreadSafe>(Type, EVoxelLandOperatorStage::Sample);
        const int32 CallsBeforeAbiCheck = ShortProviderVirtualCalls.Load();
		TestFalse(TEXT("Mismatched provider ABI is rejected"),
			FVoxelLandOperatorRegistry::RegisterV3(TEXT("VoxelLandUncooked"), Provider, 1, Error));
		TestEqual(TEXT("ABI mismatch does not invoke provider methods"), ShortProviderVirtualCalls.Load(),
			CallsBeforeAbiCheck);
		TestTrue(
			TEXT("ABI error gives rebuild guidance"), Error.Contains(TEXT("ABI")) && Error.Contains(TEXT("rebuild")));
		if (!FVoxelLandOperatorRegistry::RegisterV3(
				TEXT("VoxelLandUncooked"), Provider, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
		{
			AddError(Error);
			return false;
		}
		FVoxelLandProgramDraft Draft; Draft.FormatVersion = 2; Draft.RegisterCount = 4; Draft.Constants = {1.f};
        FVoxelLandSeedSlot Seed; Seed.NodeId = FGuid(11, 22, 33, 44); Draft.Seeds.Add(Seed);
        FVoxelLandExternalOperator Operator; Operator.Id = Provider->Id(); Operator.Version = 1; Operator.Inputs = {EVoxelLandGraphValueType::Float}; Operator.Output = Type; Operator.SeedSlot = 0;
        Draft.ExternalOperators.Add(Operator);
        Draft.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::ExternalOperator, 1, 0, 0, 0, 0)};
        uint16 OutputRegister = 1;
        if (Type == EVoxelLandGraphValueType::Int32)
        {
            Draft.Instructions.Add(Op(EVoxelLandOpcode::IntToFloat, 2, 1)); OutputRegister = 2;
        }
        else if (Type == EVoxelLandGraphValueType::Bool)
        {
            Draft.Instructions.Add(Op(EVoxelLandOpcode::LoadConstant, 2, 0, 0, 0, 0));
            Draft.Instructions.Add(Op(EVoxelLandOpcode::SelectFloat, 3, 1, 2, 2)); OutputRegister = 3;
        }
        Draft.Instructions.Add(Op(EVoxelLandOpcode::StoreDensity, 0, OutputRegister));
        for (int32 Count : {1, 2, 3, 4, 255, 256})
        {
            const FSampleResult Result = Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), Count);
            TestTrue(FString::Printf(TEXT("Short write rejected for type %d count %d"), int32(Type), Count),
                Result.Error.Code == EVoxelLandErrorCode::InvalidMathDomain && Result.Values.IsEmpty() && Result.Error.Parameter.Contains(TEXT("did not write")));
        }
        TFuture<FSampleResult> First = Async(EAsyncExecution::ThreadPool, [Draft]() { return Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), 256); });
        TFuture<FSampleResult> Second = Async(EAsyncExecution::ThreadPool, [Draft]() { return Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), 3); });
        TestTrue(TEXT("Concurrent incomplete writes both fail"), First.Get().Error.Code == EVoxelLandErrorCode::InvalidMathDomain && Second.Get().Error.Code == EVoxelLandErrorCode::InvalidMathDomain);
        TestTrue(TEXT("Short-write provider unregisters"), FVoxelLandOperatorRegistry::Unregister(Provider->Id(), 1, Error));
    }
    TSharedRef<FShortWriteProvider, ESPMode::ThreadSafe> FilterProvider = MakeShared<FShortWriteProvider, ESPMode::ThreadSafe>(EVoxelLandGraphValueType::Float, EVoxelLandOperatorStage::OneCellFilter);
	if (!FVoxelLandOperatorRegistry::RegisterV3(
			TEXT("VoxelLandUncooked"), FilterProvider, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
	{
		AddError(Error);
		return false;
	}
	FVoxelLandProgramDraft FilterDraft; FilterDraft.FormatVersion = 2; FilterDraft.RegisterCount = 1;
    FVoxelLandSeedSlot FilterSeed; FilterSeed.NodeId = FGuid(11, 22, 33, 45); FilterDraft.Seeds.Add(FilterSeed);
    FVoxelLandExternalOperator FilterOperator; FilterOperator.Id = FilterProvider->Id(); FilterOperator.Version = 1;
    FilterOperator.Stage = EVoxelLandOperatorStage::OneCellFilter; FilterOperator.Inputs = {EVoxelLandGraphValueType::Float};
    FilterOperator.Output = EVoxelLandGraphValueType::Float; FilterOperator.SeedSlot = 0;
    FilterDraft.ExternalFilterIndex = FilterDraft.ExternalOperators.Add(FilterOperator);
    FilterDraft.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::StoreDensity, 0, 0)};
    auto GenerateIncomplete = [FilterDraft]()
    {
        FVoxelLandChunkKey Key;
        return Generate(FilterDraft, FVoxelLandZoneProfile(), Key, FVoxelLandBackendOptions());
    };
    {
    TFuture<FResult> FilterFirst = Async(EAsyncExecution::ThreadPool, GenerateIncomplete);
    TFuture<FResult> FilterSecond = Async(EAsyncExecution::ThreadPool, GenerateIncomplete);
    const FResult FilterA = FilterFirst.Get();
    const FResult FilterB = FilterSecond.Get();
    TestTrue(TEXT("Concurrent partial filters fail without publishing chunks"),
        FilterA.Error.Code == EVoxelLandErrorCode::InvalidProgram && FilterB.Error.Code == EVoxelLandErrorCode::InvalidProgram &&
        !FilterA.Chunk && !FilterB.Chunk && FilterA.Error.Parameter.Contains(TEXT("did not write")));
    }
    TestTrue(TEXT("Short filter provider unregisters"), FVoxelLandOperatorRegistry::Unregister(FilterProvider->Id(), 1, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3ExternalFilterGraph, "VoxelLand.M3.ExternalFilterGraph", M3GraphFlags)
bool FVoxelLandM3ExternalFilterGraph::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Graph = SimpleGraph();
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 50, 1, true));
    Graph.Nodes.Last().OperatorId = VoxelLandExampleFilterOperatorId();
    Graph.Links.RemoveAt(Graph.Links.Num() - 1);
    Link(Graph, 2, 4, 0); Link(Graph, 4, 3, 0);
    FVoxelLandProgramDraft Draft; FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Filter graph compile failed: %s"), *Error));
        return false;
    }
    {
        const FVoxelLandOperatorRegistrySnapshot Frozen = FVoxelLandOperatorRegistry::Snapshot();
        FString UnregisterError;
        TestFalse(TEXT("Captured snapshot pins filter provider"), FVoxelLandOperatorRegistry::Unregister(VoxelLandExampleFilterOperatorId(), 1, UnregisterError));
        FVoxelLandProgramDraft FrozenDraft;
        TestTrue(TEXT("Frozen registry compiles filter"), FVoxelLandGraphCompiler::Compile(Graph, Frozen, FrozenDraft, Error));
        const bool bFrozenValid = Async(EAsyncExecution::ThreadPool, [&Frozen, FrozenDraft]()
        {
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            return !FVoxelLandProgram::Validate(FrozenDraft, Frozen, Program);
        }).Get();
        TestTrue(TEXT("Same frozen registry validates filter"), bFrozenValid);
    }
    TestTrue(TEXT("Filter is represented by a stage"), Draft.ExternalFilterIndex != INDEX_NONE && Draft.ExternalOperators[Draft.ExternalFilterIndex].Stage == EVoxelLandOperatorStage::OneCellFilter);
    const bool bRoundTrip = Async(EAsyncExecution::ThreadPool, [Graph, Draft]()
    {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program, Loaded;
        FVoxelLandSerializedProgram Payload; FString Failure;
        if (FVoxelLandProgram::Validate(Draft, Program) || !FVoxelLandProgramCodec::Pack(Graph, *Program, Payload, Failure) ||
            !FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure) || Loaded->GetFingerprint() != Program->GetFingerprint())
        {
            return false;
        }
        Payload.ExternalOperators[Payload.ExternalFilterIndex].Stage = EVoxelLandOperatorStage::Sample;
        return !FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure);
    }).Get();
    TestTrue(TEXT("Filter program serializes and reloads"), bRoundTrip);
    FVoxelLandGraphSnapshot Fanout = Graph;
    Fanout.Nodes.Add(Node(EVoxelLandBuiltinNode::Abs, 51, 1, true));
    Link(Fanout, 4, 5, 0);
    TestFalse(TEXT("Filter fanout is rejected"), FVoxelLandGraphCompiler::Compile(Fanout, Draft, Error));
    FVoxelLandGraphSnapshot Chain = Graph;
    Chain.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 52, 1, true));
    Chain.Nodes.Last().OperatorId = VoxelLandExampleFilterOperatorId();
    Chain.Links.RemoveAt(Chain.Links.Num() - 1);
    Link(Chain, 4, 5, 0); Link(Chain, 5, 3, 0);
    TestFalse(TEXT("Filter chains are rejected"), FVoxelLandGraphCompiler::Compile(Chain, Draft, Error));
    Graph.Nodes[4].OperatorVersion = 999;
    TestFalse(TEXT("Missing filter provider version is rejected"), FVoxelLandGraphCompiler::Compile(Graph, Draft, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3GraphDiagnostics, "VoxelLand.M3.GraphDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM3GraphDiagnostics::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Source = SimpleGraph();
    FVoxelLandProgramDraft Draft;
    FVoxelLandGraphDiagnostic Diagnostic;
    UVoxelLandTerrainGraph* Asset = NewObject<UVoxelLandTerrainGraph>();
    Asset->Nodes = Source.Nodes; Asset->Links = Source.Links;
    Asset->Nodes[1].Pins[0].bInvalidDefault = true;
    TestFalse(TEXT("Invalid source pin rejected"), Asset->CaptureSnapshot(Source, Diagnostic));
    TestTrue(TEXT("Structural pin diagnostic identifies its owner and pin"), Diagnostic.NodeId == Asset->Nodes[1].Id && Diagnostic.PinId == Asset->Nodes[1].Pins[0].Id);
    Asset->Nodes[1].Pins[0].bInvalidDefault = false;
    Asset->Links[0].SourcePin = FGuid::NewGuid();
    TestFalse(TEXT("Invalid link rejected"), Asset->CaptureSnapshot(Source, Diagnostic));
    TestTrue(TEXT("Missing source pin identifies source node without inventing a pin"), Diagnostic.NodeId == Asset->Links[0].SourceNode && !Diagnostic.PinId.IsValid());
    Source = SimpleGraph();
    Source.Nodes[2].Pins[0].Type = EVoxelLandGraphValueType::Bool;
    TestFalse(TEXT("Compiler rejects mismatched link"), FVoxelLandGraphCompiler::Compile(Source, Draft, Diagnostic));
    TestTrue(TEXT("Compiler identifies mismatched target pin"), Diagnostic.NodeId == Source.Nodes[2].Id && Diagnostic.PinId == Source.Nodes[2].Pins[0].Id);
    Source = SimpleGraph();
    Source.Nodes[2].OperatorId = FGuid(0x8181, 1, 2, 3);
    TestFalse(TEXT("Missing provider rejected"), FVoxelLandGraphCompiler::Compile(Source, Draft, Diagnostic));
    TestTrue(TEXT("Missing provider identifies node only"), Diagnostic.NodeId == Source.Nodes[2].Id && !Diagnostic.PinId.IsValid() && Diagnostic.Message.Contains(TEXT("provider")));
    Source = SimpleGraph();
    Source.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 50, 1, true));
    Source.Nodes.Last().OperatorId = VoxelLandExampleFilterOperatorId();
    Source.Links.RemoveAt(Source.Links.Num() - 1);
    Link(Source, 2, 4, 0); Link(Source, 4, 3, 0);
    Source.Nodes.Add(Node(EVoxelLandBuiltinNode::Abs, 51, 1, true));
    Link(Source, 4, 5, 0);
    TestFalse(TEXT("Filter fanout rejected"), FVoxelLandGraphCompiler::Compile(Source, Draft, Diagnostic));
    TestTrue(TEXT("Root filter diagnostic identifies output pin"), Diagnostic.NodeId == Source.Nodes[4].Id && Diagnostic.PinId == Source.Nodes[4].Pins.Last().Id);
    Source = SimpleGraph();
    Source.Nodes.Add(Node(EVoxelLandBuiltinNode::SmoothDensity, 60, 3, true));
    Source.Nodes[4].Pins[1].Type = EVoxelLandGraphValueType::Bool;
    Source.Nodes[4].Pins[2].FloatDefault = 2.f;
    Source.Links.RemoveAt(Source.Links.Num() - 1);
    Link(Source, 2, 4, 0); Link(Source, 4, 3, 0);
    TestFalse(TEXT("Invalid root filter strength rejected"), FVoxelLandGraphCompiler::Compile(Source, Draft, Diagnostic));
    TestTrue(TEXT("Root filter diagnostic identifies strength pin"), Diagnostic.NodeId == Source.Nodes[4].Id && Diagnostic.PinId == Source.Nodes[4].Pins[2].Id);
    Source = SimpleGraph();
    Source.Nodes[2].OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::Divide);
    Source.Links.RemoveAt(1);
    Source.Nodes[2].Pins[1].FloatDefault = 0.f;
    TestFalse(TEXT("Division by zero rejected"), FVoxelLandGraphCompiler::Compile(Source, Draft, Diagnostic));
    TestTrue(TEXT("Division diagnostic identifies denominator"), Diagnostic.NodeId == Source.Nodes[2].Id && Diagnostic.PinId == Source.Nodes[2].Pins[1].Id);
    FVoxelLandGraphSnapshot Cycle;
    Cycle.Nodes.Add(Node(EVoxelLandBuiltinNode::Abs, 101, 1, true));
    Cycle.Nodes.Add(Node(EVoxelLandBuiltinNode::Abs, 102, 1, true));
    Cycle.Nodes.Add(Node(EVoxelLandBuiltinNode::Abs, 1, 1, true));
    Cycle.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 2, 1, false));
    Link(Cycle, 0, 1, 0); Link(Cycle, 1, 0, 0); Link(Cycle, 1, 2, 0); Link(Cycle, 2, 3, 0);
    TestFalse(TEXT("Cycle rejected by compiler"), FVoxelLandGraphCompiler::Compile(Cycle, Draft, Diagnostic));
    const FGuid CycleNode = Diagnostic.NodeId, CyclePin = Diagnostic.PinId;
    TestTrue(TEXT("Cycle diagnostic identifies a cycle edge, not a blocked descendant"),
        (CycleNode == Cycle.Nodes[0].Id && CyclePin == Cycle.Nodes[0].Pins[0].Id) ||
        (CycleNode == Cycle.Nodes[1].Id && CyclePin == Cycle.Nodes[1].Pins[0].Id));
    Asset->Nodes = Cycle.Nodes; Asset->Links = Cycle.Links;
    FVoxelLandGraphDiagnostic StructuralCycle;
    TestFalse(TEXT("Cycle rejected by structural validator"), Asset->ValidateStructure(StructuralCycle));
    TestTrue(TEXT("Structural and compiler diagnostics choose same cycle edge"), StructuralCycle.NodeId == CycleNode && StructuralCycle.PinId == CyclePin);
    Algo::Reverse(Cycle.Nodes); Algo::Reverse(Cycle.Links);
    TestFalse(TEXT("Reordered cycle rejected"), FVoxelLandGraphCompiler::Compile(Cycle, Draft, Diagnostic));
    TestTrue(TEXT("Cycle edge choice independent of record order"), Diagnostic.NodeId == CycleNode && Diagnostic.PinId == CyclePin);
    Source.Nodes[0].Id = Source.Nodes[1].Id;
    TestFalse(TEXT("Duplicate node IDs rejected"), FVoxelLandGraphCompiler::Compile(Source, Draft, Diagnostic));
    TestFalse(TEXT("Ambiguous duplicate node ID is not presented as a focus target"), Diagnostic.NodeId.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3RegisterOptimization, "VoxelLand.M3.RegisterOptimization", M3GraphFlags)
bool FVoxelLandM3RegisterOptimization::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    for (int32 Index = 0; Index < 600; ++Index)
    {
        Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Add, Index + 2, 2, true));
        Graph.Nodes.Last().Pins[1].FloatDefault = 1.f;
        Link(Graph, Index, Index + 1, 0);
    }
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 603, 1, false));
    Link(Graph, 600, 601, 0);
    FVoxelLandProgramDraft Draft;
    FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Long graph compile failed: %s"), *Error));
        return false;
    }
    TestTrue(TEXT("More than 1024 virtual registers reuse bounded physical registers"), Draft.RegisterCount <= 4);
    FVoxelLandZoneProfile Profile;
    Profile.VoxelSizeCm = 1024.f;
    const FSampleResult Result = Sample(Draft, EVoxelLandVMBackend::Scalar, Profile, 1);
    TestTrue(TEXT("Long optimized graph evaluates in original order"), !Result.Error && Result.Values.Num() == 1 && Result.Values[0] == -129.f * 106.f + 600.f);

    Graph.Nodes[0] = Node(EVoxelLandBuiltinNode::FloatConstant, 1, 1, true);
    Graph.Nodes[0].Pins[0].FloatDefault = 5.f;
    Graph.Links[0].SourcePin = Graph.Nodes[0].Pins.Last().Id;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Folded graph compile failed: %s"), *Error));
        return false;
    }
    TestEqual(TEXT("Folded graph retains one constant"), Draft.Constants.Num(), 1);
    TestEqual(TEXT("Folded graph removes dead operand loads"), Draft.Instructions.Num(), 2);
    const FSampleResult Folded = Sample(Draft, EVoxelLandVMBackend::Scalar, Profile, 1);
    TestTrue(TEXT("Folded graph evaluates correctly"), !Folded.Error && Folded.Values.Num() == 1 && Folded.Values[0] == 605.f);

    FVoxelLandGraphSnapshot Domain;
    Domain.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 1001, 1, true));
    Domain.Nodes[0].Pins[0].FloatDefault = 1.f;
    Domain.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1002, 0, true));
    Domain.Nodes.Add(Node(EVoxelLandBuiltinNode::Divide, 1003, 2, true));
    Domain.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 1004, 1, true));
    Domain.Nodes.Add(Node(EVoxelLandBuiltinNode::Multiply, 1005, 2, true));
    Domain.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 1006, 1, false));
    Link(Domain, 0, 2, 0); Link(Domain, 1, 2, 1);
    Link(Domain, 2, 4, 0); Link(Domain, 3, 4, 1); Link(Domain, 4, 5, 0);
    if (!FVoxelLandGraphCompiler::Compile(Domain, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Domain graph compile failed: %s"), *Error));
        return false;
    }
    const FSampleResult DomainResult = Sample(Draft, EVoxelLandVMBackend::Scalar, Profile, 130);
    TestEqual(TEXT("Multiplying by zero does not erase an earlier domain failure"),
        int32(DomainResult.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3TypedDomains, "VoxelLand.M3.TypedAndDomains", M3GraphFlags)
bool FVoxelLandM3TypedDomains::RunTest(const FString& Parameters)
{
    FVoxelLandZoneProfile Profile;
    Profile.VoxelSizeCm = 1024.f;
    Profile.IntOverrides.Add(TEXT("amount"), 123);
    Profile.BoolOverrides.Add(TEXT("enabled"), true);
    const FSampleResult Typed = Sample(TypedSelect(), EVoxelLandVMBackend::Scalar, Profile, 1);
    TestTrue(TEXT("Typed overrides and float select produce expected value"), !Typed.Error && Typed.Values.Num() == 1 && Typed.Values[0] == 123);
    Profile.IntOverrides[TEXT("amount")] = 16777217;
    const FSampleResult Loss = Sample(TypedSelect(), EVoxelLandVMBackend::Scalar, Profile, 1);
    TestEqual(TEXT("Int-to-float precision loss is rejected"), int32(Loss.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
    FVoxelLandProgramDraft Divide;
    Divide.FormatVersion = 2;
    Divide.RegisterCount = 3;
    Divide.Constants = {1.f, 0.f};
    Divide.Instructions = {Op(EVoxelLandOpcode::LoadConstant, 0, 0, 0, 0, 0), Op(EVoxelLandOpcode::LoadConstant, 1, 0, 0, 0, 1),
        Op(EVoxelLandOpcode::Divide, 2, 0, 1), Op(EVoxelLandOpcode::StoreDensity, 0, 2)};
    const FSampleResult Zero = Sample(Divide, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), 1);
    TestEqual(TEXT("Division by zero is a typed evaluation error"), int32(Zero.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
    return true;
}

namespace
{
FVoxelLandGraphSnapshot ReferenceMathGraph(EVoxelLandBuiltinNode Kind, float Alpha)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Divide, 2, 2, true));
    Graph.Nodes[1].Pins[1].FloatDefault = 106.f;
    const bool bUnary = Kind == EVoxelLandBuiltinNode::Abs || Kind == EVoxelLandBuiltinNode::Negate || Kind == EVoxelLandBuiltinNode::Saturate;
    const int32 Inputs = bUnary ? 1 : (Kind == EVoxelLandBuiltinNode::Clamp || Kind == EVoxelLandBuiltinNode::Lerp ? 3 : 2);
    Graph.Nodes.Add(Node(Kind, 3, Inputs, true));
    if (Inputs >= 2) { Graph.Nodes[2].Pins[1].FloatDefault = Kind == EVoxelLandBuiltinNode::Clamp ? -1.f : 2.f; }
    if (Kind == EVoxelLandBuiltinNode::Clamp) { Graph.Nodes[2].Pins[2].FloatDefault = 1.f; }
    if (Kind == EVoxelLandBuiltinNode::Lerp) { Graph.Nodes[2].Pins[2].FloatDefault = Alpha; }
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 4, 1, false));
    Link(Graph, 0, 1, 0); Link(Graph, 1, 2, 0); Link(Graph, 2, 3, 0);
    return Graph;
}
float ReferenceMath(EVoxelLandBuiltinNode Kind, float X, float Alpha)
{
    switch (Kind)
    {
        case EVoxelLandBuiltinNode::Add: return X + 2.f;
        case EVoxelLandBuiltinNode::Subtract: return X - 2.f;
        case EVoxelLandBuiltinNode::Multiply: return X * 2.f;
        case EVoxelLandBuiltinNode::Divide: return X / 2.f;
        case EVoxelLandBuiltinNode::Min: return X < 2.f ? X : 2.f;
        case EVoxelLandBuiltinNode::Max: return X > 2.f ? X : 2.f;
        case EVoxelLandBuiltinNode::Abs: return X < 0.f ? -X : X;
        case EVoxelLandBuiltinNode::Negate: return -X;
        case EVoxelLandBuiltinNode::Clamp: return X < -1.f ? -1.f : (X > 1.f ? 1.f : X);
        case EVoxelLandBuiltinNode::Saturate: return X < 0.f ? 0.f : (X > 1.f ? 1.f : X);
        case EVoxelLandBuiltinNode::Lerp: return X + (2.f - X) * Alpha;
        default: return 0.f;
    }
}
FVoxelLandGraphSnapshot ReferenceControlGraph(EVoxelLandBuiltinNode Kind, bool Other)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Less, 2, 2, true));
    Graph.Nodes[1].Pins[2].Type = EVoxelLandGraphValueType::Bool;
    Link(Graph, 0, 1, 0);
    const bool bUnary = Kind == EVoxelLandBuiltinNode::BoolNot;
    const bool bLogic = bUnary || Kind == EVoxelLandBuiltinNode::BoolAnd || Kind == EVoxelLandBuiltinNode::BoolOr;
    const bool bCompare = Kind == EVoxelLandBuiltinNode::Less || Kind == EVoxelLandBuiltinNode::LessEqual || Kind == EVoxelLandBuiltinNode::Equal;
    if (bCompare)
    {
        Graph.Nodes[1].OperatorId = VoxelLandBuiltinNodeId(Kind);
    }
    else if (bLogic)
    {
        const int32 InputCount = bUnary ? 1 : 2;
        Graph.Nodes.Add(Node(Kind, 3, InputCount, true));
        for (FVoxelLandGraphPinRecord& Pin : Graph.Nodes.Last().Pins) { Pin.Type = EVoxelLandGraphValueType::Bool; }
        if (!bUnary) { Graph.Nodes.Last().Pins[1].BoolDefault = Other; }
        Link(Graph, 1, 2, 0);
    }
    const int32 Condition = Graph.Nodes.Num() - 1;
    if (Kind == EVoxelLandBuiltinNode::SelectInt)
    {
        Graph.Nodes.Add(Node(Kind, 4, 3, true));
        Graph.Nodes.Last().Pins[0].Type = EVoxelLandGraphValueType::Bool;
        for (int32 I = 1; I < 4; ++I) { Graph.Nodes.Last().Pins[I].Type = EVoxelLandGraphValueType::Int32; }
        Graph.Nodes.Last().Pins[1].IntDefault = 7; Graph.Nodes.Last().Pins[2].IntDefault = -5;
        Link(Graph, Condition, Graph.Nodes.Num() - 1, 0);
        Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::IntToFloat, 5, 1, true));
        Graph.Nodes.Last().Pins[0].Type = EVoxelLandGraphValueType::Int32;
        Link(Graph, Graph.Nodes.Num() - 2, Graph.Nodes.Num() - 1, 0);
    }
    else
    {
        if (Kind == EVoxelLandBuiltinNode::SelectBool)
        {
            Graph.Nodes.Add(Node(Kind, 4, 3, true));
            for (FVoxelLandGraphPinRecord& Pin : Graph.Nodes.Last().Pins) { Pin.Type = EVoxelLandGraphValueType::Bool; }
            Graph.Nodes.Last().Pins[1].BoolDefault = Other;
            Graph.Nodes.Last().Pins[2].BoolDefault = !Other;
            Link(Graph, Condition, Graph.Nodes.Num() - 1, 0);
        }
        Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::SelectFloat, 6, 3, true));
        Graph.Nodes.Last().Pins[0].Type = EVoxelLandGraphValueType::Bool;
        Graph.Nodes.Last().Pins[1].FloatDefault = 7.f; Graph.Nodes.Last().Pins[2].FloatDefault = -5.f;
        Link(Graph, Graph.Nodes.Num() - 2, Graph.Nodes.Num() - 1, 0);
    }
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 7, 1, false));
    Link(Graph, Graph.Nodes.Num() - 2, Graph.Nodes.Num() - 1, 0);
    return Graph;
}
}
namespace
{
void CheckReferenceMath(FAutomationTestBase& Test, const FVoxelLandZoneProfile& ReferenceProfile)
{
    const EVoxelLandBuiltinNode MathKinds[] = {EVoxelLandBuiltinNode::Add, EVoxelLandBuiltinNode::Subtract,
        EVoxelLandBuiltinNode::Multiply, EVoxelLandBuiltinNode::Divide, EVoxelLandBuiltinNode::Min,
        EVoxelLandBuiltinNode::Max, EVoxelLandBuiltinNode::Abs, EVoxelLandBuiltinNode::Negate,
        EVoxelLandBuiltinNode::Clamp, EVoxelLandBuiltinNode::Saturate, EVoxelLandBuiltinNode::Lerp};
    for (EVoxelLandBuiltinNode Kind : MathKinds)
    {
        const TArray<float> Alphas = Kind == EVoxelLandBuiltinNode::Lerp ? TArray<float>{-1.f, 0.f, 0.5f, 1.f, 2.f} : TArray<float>{0.f};
        for (float Alpha : Alphas)
        {
            FVoxelLandProgramDraft Draft; FString Error;
            if (!FVoxelLandGraphCompiler::Compile(ReferenceMathGraph(Kind, Alpha), Draft, Error))
            {
                Test.AddError(FString::Printf(TEXT("Math kind %d compilation: %s"), int32(Kind), *Error));
                continue;
            }
            for (int32 Count : {1, 2, 3, 256})
            {
                for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
                {
                    const FSampleResult Actual = Sample(Draft, Backend, ReferenceProfile, Count);
                    if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable)
                    {
                        if (Kind == EVoxelLandBuiltinNode::Add && Count == 256)
                        {
                            Test.AddWarning(TEXT("Forced VM SIMD4 unavailable; SIMD reference comparisons were not exercised"));
                        }
                        continue;
                    }
                    if (Kind == EVoxelLandBuiltinNode::Add && Count == 256)
                    {
                        Test.AddInfo(FString::Printf(TEXT("Reference math requested backend %d, actual backend %d"), int32(Backend), int32(Actual.VM)));
                        if (Backend != EVoxelLandVMBackend::Auto)
                        {
                            Test.TestEqual(TEXT("Requested explicit backend selected"), int32(Actual.VM), int32(Backend));
                        }
                    }
                    bool bMatch = !Actual.Error && Actual.Values.Num() == Count;
                    for (int32 I = 0; bMatch && I < Count; ++I)
                    {
                        bMatch = Actual.Values[I] == int16(std::round(ReferenceMath(Kind, float(I - 129), Alpha)));
                    }
                    Test.TestTrue(FString::Printf(TEXT("Math kind %d alpha %.1f count %d backend %d matches reference"), int32(Kind), Alpha, Count, int32(Backend)), bMatch);
                }
            }
        }
    }
}
void CheckReferenceControl(FAutomationTestBase& Test, const FVoxelLandZoneProfile& ReferenceProfile)
{
    const EVoxelLandBuiltinNode ControlKinds[] = {EVoxelLandBuiltinNode::Less, EVoxelLandBuiltinNode::LessEqual,
        EVoxelLandBuiltinNode::Equal, EVoxelLandBuiltinNode::BoolAnd, EVoxelLandBuiltinNode::BoolOr,
        EVoxelLandBuiltinNode::BoolNot, EVoxelLandBuiltinNode::SelectFloat, EVoxelLandBuiltinNode::SelectInt,
        EVoxelLandBuiltinNode::SelectBool};
    for (EVoxelLandBuiltinNode Kind : ControlKinds)
    {
        for (bool Other : {false, true})
        {
            FVoxelLandProgramDraft Draft; FString Error;
            if (!FVoxelLandGraphCompiler::Compile(ReferenceControlGraph(Kind, Other), Draft, Error))
            {
                Test.AddError(FString::Printf(TEXT("Control kind %d compilation: %s"), int32(Kind), *Error));
                continue;
            }
            for (int32 Count : {1, 2, 3, 256})
            {
                for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
                {
                    const FSampleResult Actual = Sample(Draft, Backend, ReferenceProfile, Count);
                    if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable) { continue; }
                    bool bMatch = !Actual.Error && Actual.Values.Num() == Count;
                    for (int32 I = 0; bMatch && I < Count; ++I)
                    {
                        const int32 X = I - 129;
                        bool Condition = X < 0;
                        if (Kind == EVoxelLandBuiltinNode::LessEqual) { Condition = X <= 0; }
                        if (Kind == EVoxelLandBuiltinNode::Equal) { Condition = X == 0; }
                        if (Kind == EVoxelLandBuiltinNode::BoolAnd) { Condition = Condition && Other; }
                        if (Kind == EVoxelLandBuiltinNode::BoolOr) { Condition = Condition || Other; }
                        if (Kind == EVoxelLandBuiltinNode::BoolNot) { Condition = !Condition; }
                        if (Kind == EVoxelLandBuiltinNode::SelectBool) { Condition = Condition ? Other : !Other; }
                        bMatch = Actual.Values[I] == (Condition ? 7.f : -5.f);
                    }
                    Test.TestTrue(FString::Printf(TEXT("Control kind %d flag %d count %d backend %d matches reference"), int32(Kind), int32(Other), Count, int32(Backend)), bMatch);
                }
            }
        }
    }
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3ReferenceOperations, "VoxelLand.M3.ReferenceOperations", M3GraphFlags)
bool FVoxelLandM3ReferenceOperations::RunTest(const FString& Parameters)
{
    FVoxelLandZoneProfile ReferenceProfile;
    ReferenceProfile.VoxelSizeCm = 1024.f;
    CheckReferenceMath(*this, ReferenceProfile);
    CheckReferenceControl(*this, ReferenceProfile);
    FVoxelLandGraphSnapshot BadClamp = ReferenceMathGraph(EVoxelLandBuiltinNode::Clamp, 0.f);
    BadClamp.Nodes[2].Pins[1].FloatDefault = 2.f; BadClamp.Nodes[2].Pins[2].FloatDefault = -2.f;
    FVoxelLandProgramDraft Invalid; FString Error;
    TestFalse(TEXT("Inverted constant clamp fails compilation"), FVoxelLandGraphCompiler::Compile(BadClamp, Invalid, Error));
    FVoxelLandGraphSnapshot DynamicClamp = ReferenceMathGraph(EVoxelLandBuiltinNode::Clamp, 0.f);
    DynamicClamp.Nodes[2].Pins[2].FloatDefault = 0.f;
    Link(DynamicClamp, 1, 2, 1);
    if (TestTrue(TEXT("Dynamic clamp compiles"), FVoxelLandGraphCompiler::Compile(DynamicClamp, Invalid, Error)))
    {
        for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
        {
            const FSampleResult Actual = Sample(Invalid, Backend, ReferenceProfile, 256);
            if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable) { continue; }
            TestEqual(TEXT("Inverted dynamic clamp returns math-domain error"), int32(Actual.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
            TestTrue(TEXT("Invalid clamp publishes no partial samples"), Actual.Values.IsEmpty());
        }
    }
    FVoxelLandGraphSnapshot BadDivide = ReferenceMathGraph(EVoxelLandBuiltinNode::Divide, 0.f);
    BadDivide.Nodes[2].Pins[1].FloatDefault = 0.f;
    TestFalse(TEXT("Constant division by zero fails compilation"), FVoxelLandGraphCompiler::Compile(BadDivide, Invalid, Error));
    FVoxelLandGraphSnapshot DynamicDivide = ReferenceMathGraph(EVoxelLandBuiltinNode::Divide, 0.f);
    Link(DynamicDivide, 1, 2, 1);
    if (TestTrue(TEXT("Dynamic divide compiles"), FVoxelLandGraphCompiler::Compile(DynamicDivide, Invalid, Error)))
    {
        for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
        {
            const FSampleResult Actual = Sample(Invalid, Backend, ReferenceProfile, 256);
            if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable) { continue; }
            TestEqual(TEXT("Dynamic division by zero returns math-domain error"), int32(Actual.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
            TestTrue(TEXT("Invalid division publishes no partial samples"), Actual.Values.IsEmpty());
        }
    }
    return true;
}

namespace
{
FVoxelLandGraphSnapshot ReferenceShapeGraph(EVoxelLandBuiltinNode Kind, bool bCorner)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 101, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Divide, 102, 2, true));
    Graph.Nodes[1].Pins[1].FloatDefault = 106.f;
    const int32 Inputs = Kind == EVoxelLandBuiltinNode::Plane ? 7 : Kind == EVoxelLandBuiltinNode::Sphere ? 4 : 6;
    Graph.Nodes.Add(Node(Kind, 103, Inputs, true));
    if (Kind == EVoxelLandBuiltinNode::Plane)
    {
        Graph.Nodes[2].Pins[3].FloatDefault = 1.f;
    }
    else if (Kind == EVoxelLandBuiltinNode::Sphere)
    {
        Graph.Nodes[2].Pins[3].FloatDefault = 3.f;
        if (bCorner) { Graph.Nodes[2].Pins[1].FloatDefault = 4.f; Graph.Nodes[2].Pins[2].FloatDefault = 6.f; }
    }
    else
    {
        Graph.Nodes[2].Pins[3].FloatDefault = 3.f; Graph.Nodes[2].Pins[4].FloatDefault = 2.f; Graph.Nodes[2].Pins[5].FloatDefault = 4.f;
        if (bCorner) { Graph.Nodes[2].Pins[1].FloatDefault = 4.f; Graph.Nodes[2].Pins[2].FloatDefault = 6.f; }
    }
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 104, 1, false));
    Link(Graph, 0, 1, 0); Link(Graph, 1, 2, 0); Link(Graph, 2, 3, 0);
    return Graph;
}
int16 ReferenceShapeValue(EVoxelLandBuiltinNode Kind, int32 X, bool bCorner)
{
    const double Position = double(X);
    double Value = Position;
    if (Kind == EVoxelLandBuiltinNode::Sphere)
    {
        const double Y = bCorner ? 4.0 : 0.0;
        const double Z = bCorner ? 6.0 : 0.0;
        Value = std::sqrt(Position * Position + Y * Y + Z * Z) - 3.0;
    }
    else if (Kind == EVoxelLandBuiltinNode::Box)
    {
        const double DX = std::abs(Position) - 3.0;
        const double DY = bCorner ? 2.0 : -2.0;
        const double DZ = bCorner ? 2.0 : -4.0;
        const double PX = std::max(DX, 0.0), PY = std::max(DY, 0.0), PZ = std::max(DZ, 0.0);
        Value = std::sqrt(PX * PX + PY * PY + PZ * PZ) + std::min(std::max(DX, std::max(DY, DZ)), 0.0);
    }
    return int16(std::round(Value));
}
FVoxelLandGraphSnapshot ReferenceCsgGraph(EVoxelLandBuiltinNode Kind)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 201, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Divide, 202, 2, true));
    Graph.Nodes[1].Pins[1].FloatDefault = 106.f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Multiply, 203, 2, true));
    Graph.Nodes[2].Pins[1].FloatDefault = -2.f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Subtract, 204, 2, true));
    Graph.Nodes[3].Pins[1].FloatDefault = 2.f;
    Graph.Nodes.Add(Node(Kind, 205, 2, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 206, 1, false));
    Link(Graph, 0, 1, 0); Link(Graph, 1, 2, 0); Link(Graph, 2, 3, 0);
    Link(Graph, 1, 4, 0); Link(Graph, 3, 4, 1); Link(Graph, 4, 5, 0);
    return Graph;
}
int16 ReferenceCsgValue(EVoxelLandBuiltinNode Kind, int32 X)
{
    const int32 A = X, B = -2 * X - 2;
    if (Kind == EVoxelLandBuiltinNode::Union) { return int16(std::min(A, B)); }
    if (Kind == EVoxelLandBuiltinNode::Intersect) { return int16(std::max(A, B)); }
    return int16(std::max(A, -B));
}
void CheckReferenceShape(FAutomationTestBase& Test, EVoxelLandBuiltinNode Kind, bool bCorner,
    const FVoxelLandZoneProfile& Profile)
{
    FVoxelLandProgramDraft Draft; FString Error;
    if (!FVoxelLandGraphCompiler::Compile(ReferenceShapeGraph(Kind, bCorner), Draft, Error))
    {
        Test.AddError(FString::Printf(TEXT("Shape %d compilation: %s"), int32(Kind), *Error));
        return;
    }
    for (int32 Count : {1, 2, 3, 256})
    {
        for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
        {
            const FSampleResult Actual = Sample(Draft, Backend, Profile, Count);
            if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable)
            {
                if (Kind == EVoxelLandBuiltinNode::Plane && Count == 256) { Test.AddWarning(TEXT("SIMD4 unavailable for shape reference cases")); }
                continue;
            }
            if (Kind == EVoxelLandBuiltinNode::Plane && Count == 256)
            {
                Test.AddInfo(FString::Printf(TEXT("Shape reference requested backend %d, actual backend %d"), int32(Backend), int32(Actual.VM)));
                if (Backend != EVoxelLandVMBackend::Auto)
                {
                    Test.TestEqual(TEXT("Explicit shape backend selected"), int32(Actual.VM), int32(Backend));
                }
            }
            bool bMatch = !Actual.Error && Actual.Values.Num() == Count;
            for (int32 I = 0; bMatch && I < Count; ++I)
            {
                bMatch = Actual.Values[I] == ReferenceShapeValue(Kind, I - 129, bCorner);
            }
            Test.TestTrue(FString::Printf(TEXT("Shape %d corner %d count %d backend %d matches reference"), int32(Kind), int32(bCorner), Count, int32(Backend)), bMatch);
        }
    }
}
void CheckReferenceCsg(FAutomationTestBase& Test, EVoxelLandBuiltinNode Kind, const FVoxelLandZoneProfile& Profile)
{
    bool bFirstWins = false, bSecondWins = false;
    for (int32 X = -129; X <= 126; ++X)
    {
        const int32 A = X, B = -2 * X - 2;
        const int32 Second = Kind == EVoxelLandBuiltinNode::SubtractShape ? -B : B;
        const bool bFirst = Kind == EVoxelLandBuiltinNode::Union ? A < Second : A > Second;
        const bool bSecond = Kind == EVoxelLandBuiltinNode::Union ? Second < A : Second > A;
        bFirstWins |= bFirst;
        bSecondWins |= bSecond;
    }
    Test.TestTrue(TEXT("Both CSG operands win across reference samples"), bFirstWins && bSecondWins);
    FVoxelLandProgramDraft Draft; FString Error;
    if (!FVoxelLandGraphCompiler::Compile(ReferenceCsgGraph(Kind), Draft, Error))
    {
        Test.AddError(FString::Printf(TEXT("CSG %d compilation: %s"), int32(Kind), *Error));
        return;
    }
    for (int32 Count : {1, 2, 3, 256})
    {
        for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
        {
            const FSampleResult Actual = Sample(Draft, Backend, Profile, Count);
            if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable) { continue; }
            bool bMatch = !Actual.Error && Actual.Values.Num() == Count;
            for (int32 I = 0; bMatch && I < Count; ++I)
            {
                bMatch = Actual.Values[I] == ReferenceCsgValue(Kind, I - 129);
            }
            Test.TestTrue(FString::Printf(TEXT("CSG %d count %d backend %d matches reference"), int32(Kind), Count, int32(Backend)), bMatch);
        }
    }
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3ReferenceShapes, "VoxelLand.M3.ReferenceShapes", M3GraphFlags)
bool FVoxelLandM3ReferenceShapes::RunTest(const FString& Parameters)
{
    FVoxelLandZoneProfile Profile; Profile.VoxelSizeCm = 1024.f;
    for (EVoxelLandBuiltinNode Kind : {EVoxelLandBuiltinNode::Plane, EVoxelLandBuiltinNode::Sphere, EVoxelLandBuiltinNode::Box})
    {
        CheckReferenceShape(*this, Kind, false, Profile);
        if (Kind != EVoxelLandBuiltinNode::Plane) { CheckReferenceShape(*this, Kind, true, Profile); }
    }
    for (EVoxelLandBuiltinNode Kind : {EVoxelLandBuiltinNode::Union, EVoxelLandBuiltinNode::SubtractShape, EVoxelLandBuiltinNode::Intersect})
    {
        CheckReferenceCsg(*this, Kind, Profile);
    }
    for (EVoxelLandBuiltinNode Kind : {EVoxelLandBuiltinNode::Sphere, EVoxelLandBuiltinNode::Box, EVoxelLandBuiltinNode::Plane})
    {
        FVoxelLandGraphSnapshot InvalidGraph = ReferenceShapeGraph(Kind, false);
        InvalidGraph.Nodes[2].Pins[3].FloatDefault = 0.f;
        FVoxelLandProgramDraft Draft; FString Error;
        TestFalse(TEXT("Invalid constant shape parameter fails compilation"), FVoxelLandGraphCompiler::Compile(InvalidGraph, Draft, Error));
        FVoxelLandGraphSnapshot Dynamic = ReferenceShapeGraph(Kind, false);
        Dynamic.Nodes.Insert(Node(EVoxelLandBuiltinNode::Negate, 105, 1, true), 3);
        Link(Dynamic, 1, 3, 0); Link(Dynamic, 3, 2, 3);
        if (TestTrue(TEXT("Dynamic shape parameter compiles"), FVoxelLandGraphCompiler::Compile(Dynamic, Draft, Error)))
        {
            for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
            {
                if (Kind != EVoxelLandBuiltinNode::Plane)
                {
                    const FSampleResult Valid = Sample(Draft, Backend, Profile, 129);
                    if (Backend == EVoxelLandVMBackend::Simd4 && Valid.Error.Code == EVoxelLandErrorCode::BackendUnavailable) { continue; }
                    TestTrue(TEXT("Positive dynamic radius or extent evaluates at the boundary"),
                        !Valid.Error && Valid.Values.Num() == 129 && !Valid.Values.ContainsByPredicate([](int16 Value) { return Value != 0; }));
                }
                const FSampleResult Actual = Sample(Draft, Backend, Profile, Kind == EVoxelLandBuiltinNode::Plane ? 1 : 130);
                if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable) { continue; }
                TestEqual(TEXT("Invalid dynamic shape parameter returns math-domain error"), int32(Actual.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
                TestTrue(TEXT("Invalid shape publishes no partial samples"), Actual.Values.IsEmpty());
            }
        }
    }
    for (int32 ExtentPin : {4, 5})
    {
        FVoxelLandGraphSnapshot InvalidBox = ReferenceShapeGraph(EVoxelLandBuiltinNode::Box, false);
        InvalidBox.Nodes[2].Pins[ExtentPin].FloatDefault = -1.f;
        FVoxelLandProgramDraft Draft; FString Error;
        TestFalse(TEXT("Negative box extent fails compilation on every axis"), FVoxelLandGraphCompiler::Compile(InvalidBox, Draft, Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3Power, "VoxelLand.M3.Power", M3GraphFlags)
bool FVoxelLandM3Power::RunTest(const FString& Parameters)
{
    uint32 SmallestBits = 1;
    float Smallest;
    FMemory::Memcpy(&Smallest, &SmallestBits, sizeof(Smallest));
    const float Bases[] = {2.f, 4.f, 0.25f, 3.f, 1024.f, Smallest, -2.f, -2.f, -0.5f};
    const float Exponents[] = {0.5f, 0.5f, 0.5f, 1.25f, 0.1f, 0.5f, 3.f, -3.f, -32.f};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bases); ++Index)
    {
        float Actual = 0.f;
        const double Expected = std::pow(double(Bases[Index]), double(Exponents[Index]));
        const bool bEvaluated = FVoxelLandPower::Evaluate(Bases[Index], Exponents[Index], Actual);
        const double Difference = FMath::Abs(double(Actual) - Expected);
        const double Tolerance = FMath::Max(FMath::Abs(Expected) * 0.000002, 1.0e-44);
        TestTrue(FString::Printf(TEXT("Power case %d matches independent reference"), Index),
            bEvaluated && Difference <= Tolerance);
    }
    float Actual = 1.f;
    TestTrue(TEXT("Zero to positive exponent succeeds"), FVoxelLandPower::Evaluate(0.f, 0.5f, Actual) && Actual == 0.f);
    TestFalse(TEXT("Zero to zero is rejected"), FVoxelLandPower::Evaluate(0.f, 0.f, Actual));
    TestFalse(TEXT("Negative base with fractional exponent is rejected"), FVoxelLandPower::Evaluate(-2.f, 0.5f, Actual));
    TestFalse(TEXT("Negative integer exponent beyond limit is rejected"), FVoxelLandPower::Evaluate(-2.f, 33.f, Actual));
    TestFalse(TEXT("Overflow is rejected"), FVoxelLandPower::Evaluate(3.f, 100.f, Actual));
    uint32 InfinityBits = 0x7f800000u;
    float Infinity;
    FMemory::Memcpy(&Infinity, &InfinityBits, sizeof(Infinity));
    TestFalse(TEXT("Infinite exponent is rejected"), FVoxelLandPower::Evaluate(2.f, Infinity, Actual));
    TestTrue(TEXT("Underflow rounds to zero"), FVoxelLandPower::Evaluate(0.5f, 200.f, Actual) && Actual == 0.f);
    TestTrue(TEXT("Subnormal identity keeps its binary32 bits"), FVoxelLandPower::Evaluate(Smallest, 1.f, Actual));
    uint32 ResultBits = 0;
    FMemory::Memcpy(&ResultBits, &Actual, sizeof(ResultBits));
    TestEqual(TEXT("Smallest subnormal remains representable"), ResultBits, SmallestBits);

    FVoxelLandProgramDraft Draft;
    Draft.FormatVersion = 2;
    Draft.RegisterCount = 3;
    Draft.Constants = {3.f, 0.5f};
    Draft.Instructions = {Op(EVoxelLandOpcode::LoadConstant, 0, 0, 0, 0, 0),
        Op(EVoxelLandOpcode::LoadConstant, 1, 0, 0, 0, 1), Op(EVoxelLandOpcode::Power, 2, 0, 1),
        Op(EVoxelLandOpcode::StoreDensity, 0, 2)};
    for (int32 Count : {1, 2, 3, 256})
    {
        const FSampleResult Scalar = Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), Count);
        const FSampleResult Simd = Sample(Draft, EVoxelLandVMBackend::Auto, FVoxelLandZoneProfile(), Count);
        TestTrue(FString::Printf(TEXT("Scalar and automatic VM match for %d samples"), Count),
            !Scalar.Error && !Simd.Error && Scalar.Values == Simd.Values);
        const FSampleResult ForcedSimd = Sample(Draft, EVoxelLandVMBackend::Simd4, FVoxelLandZoneProfile(), Count);
        const bool bUnavailable = ForcedSimd.Error.Code == EVoxelLandErrorCode::BackendUnavailable;
        TestTrue(FString::Printf(TEXT("Forced SIMD agrees or is unavailable for %d samples"), Count),
            bUnavailable || (!Scalar.Error && !ForcedSimd.Error && Scalar.Values == ForcedSimd.Values));
    }
    Draft.Constants = {-2.f, 0.5f};
    const FSampleResult Invalid = Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), 1);
    TestEqual(TEXT("Invalid sample returns typed math error"), int32(Invalid.Error.Code), int32(EVoxelLandErrorCode::InvalidMathDomain));
    FVoxelLandGraphSnapshot InvalidGraph;
    InvalidGraph.Nodes.Add(Node(EVoxelLandBuiltinNode::Power, 1, 2, true));
    InvalidGraph.Nodes[0].Pins[0].FloatDefault = -2.f;
    InvalidGraph.Nodes[0].Pins[1].FloatDefault = 0.5f;
    InvalidGraph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 2, 1, false));
    Link(InvalidGraph, 0, 1, 0);
    FString GraphError;
    TestFalse(TEXT("Invalid constant power domain fails graph compilation"),
        FVoxelLandGraphCompiler::Compile(InvalidGraph, Draft, GraphError));
    TestTrue(TEXT("Invalid power reports a domain diagnostic"), GraphError.Contains(TEXT("power")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3BoundsAndOutputs, "VoxelLand.M3.InputBoundsAndOutputs", M3GraphFlags)
bool FVoxelLandM3BoundsAndOutputs::RunTest(const FString& Parameters)
{
    FVoxelLandProgramDraft Oversized = Plane();
    Oversized.Instructions.SetNum(8193);
    const FVoxelLandError Failure = Async(EAsyncExecution::ThreadPool, [Oversized]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        return FVoxelLandProgram::Validate(Oversized, Program);
    }).Get();
    TestEqual(TEXT("Oversized raw instruction array is rejected"), int32(Failure.Code), int32(EVoxelLandErrorCode::InvalidProgram));
    TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program = Async(EAsyncExecution::ThreadPool, [] {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Output;
        FVoxelLandProgram::Validate(Plane(), Output);
        return Output;
    }).Get();
    if (!Program)
    {
        AddError(TEXT("Failed to prepare valid program"));
        return false;
    }
    const FVoxelLandError ThreadError = FVoxelLandProgram::Validate(Plane(), Program);
    TestTrue(TEXT("Wrong-thread validation clears existing output"),
        ThreadError.Code == EVoxelLandErrorCode::GameThreadNotAllowed && !Program);
    const FResult Ready = Async(EAsyncExecution::ThreadPool, [] {
        return Generate(Plane(), FVoxelLandZoneProfile(), FVoxelLandChunkKey(), FVoxelLandBackendOptions());
    }).Get();
    TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator = Ready.Generator;
    if (!Generator)
    {
        AddError(TEXT("Failed to prepare valid generator"));
        return false;
    }
    FVoxelLandGeneratorSettings Settings;
    const FVoxelLandError CreateError = FVoxelLandGenerator::Create(Settings, Generator);
    TestTrue(TEXT("Wrong-thread generator creation clears existing output"),
        CreateError.Code == EVoxelLandErrorCode::GameThreadNotAllowed && !Generator);
    const bool bWorkerPreserved = Async(EAsyncExecution::ThreadPool, [Generator = Ready.Generator]() {
        TUniquePtr<FVoxelLandWorkerContext> Worker;
        if (Generator->CreateWorker(Worker) || !Worker)
        {
            return false;
        }
        FVoxelLandWorkerContext* Original = Worker.Get();
        return Async(EAsyncExecution::Thread, [&Worker, &Generator, Original]() {
            const FVoxelLandError WrongThread = Generator->CreateWorker(Worker);
            return WrongThread.Code == EVoxelLandErrorCode::InvalidContext && Worker.Get() == Original;
        }).Get();
        // Worker destruction occurs here, on its creating thread.
    }).Get();
    TestTrue(TEXT("Cross-thread CreateWorker preserves the existing owner-bound worker"), bWorkerPreserved);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3FilterCompilation, "VoxelLand.M3.FilterCompilation", M3GraphFlags)
bool FVoxelLandM3FilterCompilation::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 2, 1, false));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::SmoothDensity, 3, 3, true));
    Graph.Nodes[2].Pins[1].Type = EVoxelLandGraphValueType::Bool;
    Graph.Nodes[2].Pins[1].BoolDefault = true;
    Graph.Nodes[2].Pins[2].FloatDefault = 0.5f;
    Link(Graph, 0, 2, 0);
    Link(Graph, 2, 1, 0);
    FVoxelLandProgramDraft Draft;
    FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Filter graph failed: %s"), *Error));
        return false;
    }
    TestTrue(TEXT("Filter lowers into a separate stage"), Draft.SmoothFilter.bPresent);
    const bool bRoundTrip = Async(EAsyncExecution::ThreadPool, [Graph, Draft]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program, Loaded;
        FVoxelLandSerializedProgram Payload;
        FString Failure;
        if (FVoxelLandProgram::Validate(Draft, Program) || !FVoxelLandProgramCodec::Pack(Graph, *Program, Payload, Failure) ||
            !FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure))
        {
            return false;
        }
        return Loaded->GetData().SmoothFilter.bPresent && Loaded->GetFingerprint() == Program->GetFingerprint();
    }).Get();
    TestTrue(TEXT("Filter stage persists with its program identity"), bRoundTrip);
    const int32 RegisterCount = Draft.RegisterCount;
    FVoxelLandGraphParameter Enabled;
    Enabled.Id = FGuid(0x2222, 1, 2, 3);
    Enabled.Name = TEXT("smooth_enabled");
    Enabled.Type = EVoxelLandGraphValueType::Bool;
    Enabled.BoolDefault = true;
    Graph.Parameters.Add(Enabled);
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::BoolParameter, 5, 0, true));
    Graph.Nodes[3].Pins[0].Type = EVoxelLandGraphValueType::Bool;
    Graph.Nodes[3].ParameterId = Enabled.Id;
    Link(Graph, 3, 2, 1);
    TestTrue(TEXT("Uniform bool parameter compiles for smoothing"), FVoxelLandGraphCompiler::Compile(Graph, Draft, Error));
    TestEqual(TEXT("Uniform parameter does not emit an unused register"), Draft.RegisterCount, RegisterCount);
    TestTrue(TEXT("Smooth enabled refers to bool parameter"), Draft.SmoothFilter.Enabled.bParameter &&
        Draft.SmoothFilter.Enabled.Index < uint32(Draft.Parameters.Num()) &&
        Draft.Parameters[Draft.SmoothFilter.Enabled.Index].Type == EVoxelLandParameterType::Bool);
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Abs, 4, 1, true));
    Link(Graph, 2, 4, 0);
    TestFalse(TEXT("A filtered field cannot feed another node"), FVoxelLandGraphCompiler::Compile(Graph, Draft, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NoiseGraph, "VoxelLand.M3.NoiseGraphLowering", M3GraphFlags)
bool FVoxelLandM3NoiseGraph::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionY, 2, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Perlin2, 3, 6, true));
    Graph.Nodes[2].Pins[2].FloatDefault = 0.01f;
    Graph.Nodes[2].Pins[3].Type = EVoxelLandGraphValueType::Int32;
    Graph.Nodes[2].Pins[3].IntDefault = 3;
    Graph.Nodes[2].Pins[4].FloatDefault = 0.5f;
    Graph.Nodes[2].Pins[5].FloatDefault = 2.f;
    Graph.Nodes[2].SeedSalt = 42;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 4, 1, false));
    Link(Graph, 0, 2, 0);
    Link(Graph, 1, 2, 1);
    Link(Graph, 2, 3, 0);
    FVoxelLandProgramDraft Draft;
    FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Noise graph failed to compile: %s"), *Error));
        return false;
    }
    TestTrue(TEXT("Noise configuration and per-node seed are lowered"), Draft.Noise.Num() == 1 && Draft.Seeds.Num() == 1 &&
        Draft.Seeds[0].NodeId == Graph.Nodes[2].Id && Draft.Seeds[0].Salt == 42u);
    const bool bReload = Async(EAsyncExecution::ThreadPool, [Graph, Draft]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program, Loaded;
        FVoxelLandSerializedProgram Payload;
        FString Failure;
        return !FVoxelLandProgram::Validate(Draft, Program) &&
            FVoxelLandProgramCodec::Pack(Graph, *Program, Payload, Failure) &&
            FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure) &&
            Program->GetFingerprint() == Loaded->GetFingerprint();
    }).Get();
    TestTrue(TEXT("Compiled noise graph round-trips"), bReload);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NoiseDefaultDiagnostics, "VoxelLand.M3.NoiseDefaultDiagnostics", M3GraphFlags)
bool FVoxelLandM3NoiseDefaultDiagnostics::RunTest(const FString& Parameters)
{
    for (bool b3D : {false, true})
    {
        const int32 Dimensions = b3D ? 3 : 2;
        FVoxelLandGraphSnapshot Graph;
        Graph.Nodes.Add(Node(b3D ? EVoxelLandBuiltinNode::Fbm3 : EVoxelLandBuiltinNode::Fbm2, 40, Dimensions + 4, true));
        Graph.Nodes[0].Pins[Dimensions].FloatDefault = 0.01f;
        Graph.Nodes[0].Pins[Dimensions + 1].Type = EVoxelLandGraphValueType::Int32;
        Graph.Nodes[0].Pins[Dimensions + 1].IntDefault = 3;
        Graph.Nodes[0].Pins[Dimensions + 2].FloatDefault = 0.5f;
        Graph.Nodes[0].Pins[Dimensions + 3].FloatDefault = 2.f;
        Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 41, 1, false));
        Link(Graph, 0, 1, 0);
        for (int32 Setting = 0; Setting < 4; ++Setting)
        {
            FVoxelLandGraphSnapshot Invalid = Graph;
            FVoxelLandGraphPinRecord& Pin = Invalid.Nodes[0].Pins[Dimensions + Setting];
            if (Setting == 0) { Pin.FloatDefault = -1.f; }
            else if (Setting == 1) { Pin.IntDefault = 17; }
            else if (Setting == 2) { Pin.FloatDefault = 1.5f; }
            else { Pin.FloatDefault = 0.5f; }
            FVoxelLandProgramDraft Draft; FVoxelLandGraphDiagnostic Diagnostic;
            TestFalse(TEXT("Out-of-range noise default fails compilation"), FVoxelLandGraphCompiler::Compile(Invalid, Draft, Diagnostic));
            TestTrue(TEXT("Noise default error identifies exact node and setting pin"),
                Diagnostic.NodeId == Invalid.Nodes[0].Id && Diagnostic.PinId == Pin.Id && !Diagnostic.Message.IsEmpty());
            TestTrue(TEXT("Invalid noise graph publishes no draft"), Draft.Instructions.IsEmpty());
        }
        FVoxelLandGraphSnapshot Linked = Graph;
        FVoxelLandGraphParameter Parameter;
        Parameter.Id = FGuid(0xabc1, 2, 3, b3D ? 4 : 5);
        Parameter.Name = TEXT("bad_frequency");
        Parameter.FloatDefault = -1.f;
        Linked.Parameters.Add(Parameter);
        Linked.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatParameter, 42, 0, true));
        Linked.Nodes.Last().ParameterId = Parameter.Id;
        Link(Linked, 2, 0, Dimensions);
        FVoxelLandProgramDraft Draft; FVoxelLandGraphDiagnostic Diagnostic;
        TestFalse(TEXT("Invalid linked parameter default fails compilation"), FVoxelLandGraphCompiler::Compile(Linked, Draft, Diagnostic));
        TestTrue(TEXT("Linked invalid frequency focuses noise input"), Diagnostic.NodeId == Linked.Nodes[0].Id &&
            Diagnostic.PinId == Linked.Nodes[0].Pins[Dimensions].Id);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3MixedNoise, "VoxelLand.M3.MixedNoiseParameters", M3GraphFlags)
bool FVoxelLandM3MixedNoise::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Graph;
    FVoxelLandGraphParameter Frequency;
    Frequency.Id = FGuid(0x1000, 1, 0, 1);
    Frequency.Name = TEXT("frequency");
    Frequency.FloatDefault = 0.01f;
    Graph.Parameters.Add(Frequency);
    FVoxelLandGraphParameter Octaves;
    Octaves.Id = FGuid(0x1000, 1, 0, 2);
    Octaves.Name = TEXT("octaves");
    Octaves.Type = EVoxelLandGraphValueType::Int32;
    Octaves.IntDefault = 3;
    Graph.Parameters.Add(Octaves);
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionY, 2, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatParameter, 3, 0, true));
    Graph.Nodes[2].ParameterId = Frequency.Id;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::IntParameter, 4, 0, true));
    Graph.Nodes[3].ParameterId = Octaves.Id;
    Graph.Nodes[3].Pins[0].Type = EVoxelLandGraphValueType::Int32;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Perlin2, 5, 6, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Fbm2, 6, 6, true));
    for (int32 NodeIndex : {4, 5})
    {
        Graph.Nodes[NodeIndex].Pins[3].Type = EVoxelLandGraphValueType::Int32;
        Graph.Nodes[NodeIndex].Pins[4].FloatDefault = 0.5f;
        Graph.Nodes[NodeIndex].Pins[5].FloatDefault = 2.f;
        Link(Graph, 0, NodeIndex, 0);
        Link(Graph, 1, NodeIndex, 1);
        Link(Graph, 2, NodeIndex, 2);
        Link(Graph, 3, NodeIndex, 3);
    }
    Graph.Nodes[4].SeedSalt = 12;
    Graph.Nodes[5].SeedSalt = 34;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Add, 7, 2, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 8, 1, false));
    Link(Graph, 4, 6, 0);
    Link(Graph, 5, 6, 1);
    Link(Graph, 6, 7, 0);
    FVoxelLandProgramDraft Draft;
    FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Mixed-noise graph compilation failed: %s"), *Error));
        return false;
    }
    TestTrue(TEXT("Mixed noises retain distinct per-node seed slots"), Draft.Seeds.Num() == 2 &&
        Draft.Seeds[0].NodeId == Graph.Nodes[4].Id && Draft.Seeds[0].Salt == 12 &&
        Draft.Seeds[1].NodeId == Graph.Nodes[5].Id && Draft.Seeds[1].Salt == 34);
    if (Draft.Seeds.Num() == 2)
    {
        const uint64 SeedA = VoxelLandDeterminism::DeriveNodeSeed(12345, Draft.Seeds[0].NodeId, Draft.Seeds[0].Salt);
        const uint64 SeedB = VoxelLandDeterminism::DeriveNodeSeed(12345, Draft.Seeds[1].NodeId, Draft.Seeds[1].Salt);
        const uint64 EditedA = VoxelLandDeterminism::DeriveNodeSeed(12345, Draft.Seeds[0].NodeId, Draft.Seeds[0].Salt + 1);
        TestTrue(TEXT("Mixed noise nodes have independently derived seeds"), SeedA != SeedB && EditedA != SeedB && SeedA != EditedA);
    }
    TestTrue(TEXT("Both noise configurations reference typed frequency and octave slots"), Draft.Noise.Num() == 2 &&
        Draft.Noise[0].Frequency.bParameter && Draft.Noise[0].Octaves.bParameter &&
        Draft.Noise[1].Frequency.bParameter && Draft.Noise[1].Octaves.bParameter &&
        Draft.Noise[0].Frequency.Index == Draft.Noise[1].Frequency.Index &&
        Draft.Noise[0].Octaves.Index == Draft.Noise[1].Octaves.Index);
    const FVoxelLandError Validation = Async(EAsyncExecution::ThreadPool, [Draft]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        return FVoxelLandProgram::Validate(Draft, Program);
    }).Get();
    TestFalse(TEXT("Mixed-noise typed program validates"), bool(Validation));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3ExternalProvider, "VoxelLand.M3.ExternalProvider", M3GraphFlags)
bool FVoxelLandM3ExternalProvider::RunTest(const FString& Parameters)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 1, 1, true));
    Graph.Nodes[0].Pins[0].FloatDefault = 3.f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Add, 2, 1, true));
    Graph.Nodes[1].OperatorId = VoxelLandSquareOperatorId();
    Graph.Nodes[1].OperatorVersion = 1;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 3, 1, false));
    Link(Graph, 0, 1, 0);
    Link(Graph, 1, 2, 0);
    FVoxelLandProgramDraft Draft;
    FString Error;
    if (!FVoxelLandGraphCompiler::Compile(Graph, Draft, Error))
    {
        AddError(FString::Printf(TEXT("Square provider did not compile: %s"), *Error));
        return false;
    }
    FVoxelLandZoneProfile Profile;
    Profile.VoxelSizeCm = 1024.f;
    const FSampleResult Scalar = Sample(Draft, EVoxelLandVMBackend::Scalar, Profile, 3);
    TestTrue(TEXT("Square provider evaluates deterministic scalar output"), !Scalar.Error && Scalar.Values.Num() == 3 &&
        Scalar.Values[0] == 9 && Scalar.Values[1] == 9 && Scalar.Values[2] == 9);
    FVoxelLandGraphSnapshot Varying = Graph;
    Varying.Nodes[0] = Node(EVoxelLandBuiltinNode::PositionX, 1, 0, true);
    Varying.Links[0].SourcePin = Varying.Nodes[0].Pins.Last().Id;
    FVoxelLandProgramDraft VaryingDraft;
    if (!FVoxelLandGraphCompiler::Compile(Varying, VaryingDraft, Error))
    {
        AddError(FString::Printf(TEXT("Varying square failed to compile: %s"), *Error));
        return false;
    }
    for (int32 Count : {1, 2, 3, 4, 7, 256})
    {
        const FSampleResult Reference = Sample(VaryingDraft, EVoxelLandVMBackend::Scalar, Profile, Count);
        const FSampleResult Automatic = Sample(VaryingDraft, EVoxelLandVMBackend::Auto, Profile, Count);
        const FSampleResult Forced = Sample(VaryingDraft, EVoxelLandVMBackend::Simd4, Profile, Count);
        TestTrue(FString::Printf(TEXT("Square scalar/automatic parity for %d samples"), Count),
            !Reference.Error && !Automatic.Error && Reference.Values == Automatic.Values);
        TestTrue(FString::Printf(TEXT("Square forced SIMD parity or unavailable for %d samples"), Count),
            Forced.Error.Code == EVoxelLandErrorCode::BackendUnavailable ||
            (!Reference.Error && !Forced.Error && Reference.Values == Forced.Values));
    }
    FVoxelLandGraphSnapshot OverflowGraph = Graph;
    OverflowGraph.Nodes[0].Pins[0].FloatDefault = FLT_MAX;
    FVoxelLandProgramDraft OverflowDraft;
    TestTrue(TEXT("Square overflow graph compiles"), FVoxelLandGraphCompiler::Compile(OverflowGraph, OverflowDraft, Error));
    const FSampleResult ScalarOverflow = Sample(OverflowDraft, EVoxelLandVMBackend::Scalar, Profile, 4);
    const FSampleResult AutoOverflow = Sample(OverflowDraft, EVoxelLandVMBackend::Auto, Profile, 4);
    TestTrue(TEXT("Square overflow returns the same scalar and automatic domain error"),
        ScalarOverflow.Error.Code == EVoxelLandErrorCode::InvalidMathDomain &&
        AutoOverflow.Error.Code == EVoxelLandErrorCode::InvalidMathDomain);

    FVoxelLandProgramDraft Aliased = Draft;
    for (FVoxelLandInstruction& Instruction : Aliased.Instructions)
    {
        if (Instruction.Op == EVoxelLandOpcode::ExternalOperator)
        {
            Instruction.Dst = Instruction.A;
            break;
        }
    }
    const FVoxelLandError AliasError = Async(EAsyncExecution::ThreadPool, [Aliased]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        return FVoxelLandProgram::Validate(Aliased, Program);
    }).Get();
    TestEqual(TEXT("External destination cannot alias an input"), int32(AliasError.Code), int32(EVoxelLandErrorCode::InvalidProgram));
    FVoxelLandGraphSnapshot Missing = Graph;
    Missing.Nodes[1].OperatorId = FGuid(0xdeadbeef, 0x10203040, 0x50607080, 0x99aabbcc);
    FVoxelLandProgramDraft Rejected;
    TestFalse(TEXT("Missing provider blocks compilation"), FVoxelLandGraphCompiler::Compile(Missing, Rejected, Error));
    Missing = Graph;
    Missing.Nodes[1].OperatorVersion = 999;
    TestFalse(TEXT("Incompatible provider version blocks compilation"), FVoxelLandGraphCompiler::Compile(Missing, Rejected, Error));
    const bool bRejectOnLoad = Async(EAsyncExecution::ThreadPool, [Graph, Draft]() {
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program, Loaded;
        FVoxelLandSerializedProgram Payload;
        FString Failure;
        if (FVoxelLandProgram::Validate(Draft, Program) || !FVoxelLandProgramCodec::Pack(Graph, *Program, Payload, Failure) ||
            Payload.ExternalOperators.Num() != 1)
        {
            return false;
        }
        if (!FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure) ||
            Loaded->GetFingerprint() != Program->GetFingerprint())
        {
            return false;
        }
        Payload.ExternalOperators[0].Id = FGuid(0xdeadbeef, 0x10203040, 0x50607080, 0x99aabbcc);
        if (FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure) || Loaded ||
            !Failure.Contains(TEXT("Missing, incompatible or invalid external operator provider")))
        {
            return false;
        }
        Payload.ExternalOperators[0].Id = VoxelLandSquareOperatorId();
        Payload.ExternalOperators[0].Version = 999;
        return !FVoxelLandProgramCodec::Load(Graph, Payload, Loaded, Failure) && !Loaded &&
            Failure.Contains(TEXT("Missing, incompatible or invalid external operator provider"));
    }).Get();
    TestTrue(TEXT("Provider round-trips and reload rejects missing and incompatible providers"), bRejectOnLoad);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3MismatchedSimd, "VoxelLand.M3.ExternalSimdFallback", M3GraphFlags)
bool FVoxelLandM3MismatchedSimd::RunTest(const FString& Parameters)
{
    FString Error;
    TSharedRef<FMismatchedSimdProvider, ESPMode::ThreadSafe> Provider = MakeShared<FMismatchedSimdProvider, ESPMode::ThreadSafe>();
	if (!FVoxelLandOperatorRegistry::RegisterV3(
			FName(TEXT("VoxelLandUncooked")), Provider, FVoxelLandOperatorRegistry::ProviderAbiVersion, Error))
	{
        AddError(FString::Printf(TEXT("Mismatch provider registration failed: %s"), *Error));
        return false;
    }
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::FloatConstant, 1, 1, true));
    Graph.Nodes[0].Pins[0].FloatDefault = 3.f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Add, 2, 1, true));
    Graph.Nodes[1].OperatorId = Provider->Id();
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 3, 1, false));
    Link(Graph, 0, 1, 0);
    Link(Graph, 1, 2, 0);
    FVoxelLandProgramDraft Draft;
    const bool bCompiled = FVoxelLandGraphCompiler::Compile(Graph, Draft, Error);
    TestTrue(TEXT("Mismatch provider compiles"), bCompiled);
    if (bCompiled)
    {
        AddExpectedMessage(TEXT("VoxelLand external SIMD kernel disabled"), ELogVerbosity::Warning,
            EAutomationExpectedMessageFlags::Contains, -1, false);
        FVoxelLandZoneProfile Profile;
        Profile.VoxelSizeCm = 1024.f;
        MismatchedSimdCalls = 0;
        const int32 Result = Async(EAsyncExecution::ThreadPool, [Draft, Profile]() {
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            if (FVoxelLandProgram::Validate(Draft, Program))
            {
                return 0;
            }
            FVoxelLandGeneratorSettings Settings;
            Settings.Program = Program;
            Settings.Profile = Profile;
            Settings.Backends.VM = EVoxelLandVMBackend::Auto;
            TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
            if (FVoxelLandGenerator::Create(Settings, Generator))
            {
                return 0;
            }
            TUniquePtr<FVoxelLandWorkerContext> Worker;
            if (Generator->CreateWorker(Worker))
            {
                return 0;
            }
            if (Worker->GetVMBackend() != EVoxelLandVMBackend::Simd4)
            {
                return 1;
            }
            TArray<FVector3f> Points;
            Points.Init(FVector3f::ZeroVector, 7);
            TArray<int16> First, Second;
            if (Generator->EvaluateSamples(Points, *Worker, First) ||
                Generator->EvaluateSamples(Points, *Worker, Second) || First != Second || First.Num() != 7)
            {
                return 0;
            }
            for (int16 Value : First)
            {
                if (Value != 3)
                {
                    return 0;
                }
            }
            // The mismatching SIMD path must be disabled for this worker after its first batch.
            return MismatchedSimdCalls.Load() == 1 ? 2 : 0;
        }).Get();
        const int32 SimdCallsBeforeShort = MismatchedSimdCalls.Load();
        ShortSimdOutput.Store(true);
        const FSampleResult ShortSimd = Sample(Draft, EVoxelLandVMBackend::Auto, Profile, 7);
        ShortSimdOutput.Store(false);
        TestTrue(TEXT("Short SIMD writes fall back to complete scalar output"), !ShortSimd.Error &&
            ShortSimd.Values.Num() == 7 && ShortSimd.Values[0] == 3);
        if (ShortSimd.VM == EVoxelLandVMBackend::Simd4)
        {
            TestTrue(TEXT("Incomplete SIMD candidate was exercised"), MismatchedSimdCalls.Load() > SimdCallsBeforeShort);
        }
        if (Result == 1)
        {
            AddWarning(TEXT("VM SIMD is unavailable; persistent fallback was not exercised"));
        }
        else
        {
            TestEqual(TEXT("SIMD mismatch disables the kernel for subsequent batches on the same worker"), Result, 2);
        }
    }
    const bool bUnregistered = FVoxelLandOperatorRegistry::Unregister(Provider->Id(), Provider->Version(), Error);
    TestTrue(TEXT("Mismatch provider unregisters after workers complete"), bUnregistered);
    return true;
}

namespace
{
FVoxelLandGraphSnapshot NativeOracleGraph(EVoxelLandBuiltinNode Kind, bool b3D)
{
    FVoxelLandGraphSnapshot Graph;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionX, 301, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionY, 302, 0, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::PositionZ, 303, 0, true));
    const int32 UniformStart = b3D ? 3 : 2;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Graph.Nodes.Add(Node(Kind, 304 + Index, UniformStart + 4, true));
        FVoxelLandGraphNodeRecord& NoiseNode = Graph.Nodes.Last();
        NoiseNode.SeedSalt = Index == 0 ? 13 : 67;
        NoiseNode.Pins[UniformStart].FloatDefault = Index == 0 ? 1.f / 128.f : 1.f / 256.f;
        NoiseNode.Pins[UniformStart + 1].Type = EVoxelLandGraphValueType::Int32;
        NoiseNode.Pins[UniformStart + 1].IntDefault = Index == 0 ? 4 : 2;
        NoiseNode.Pins[UniformStart + 2].FloatDefault = Index == 0 ? 0.375f : 0.75f;
        NoiseNode.Pins[UniformStart + 3].FloatDefault = Index == 0 ? 2.f : 3.f;
        const int32 Target = Graph.Nodes.Num() - 1;
        Link(Graph, 2, Target, 0); Link(Graph, 0, Target, 1);
        if (b3D) { Link(Graph, 1, Target, 2); }
    }
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Multiply, 306, 2, true));
    Graph.Nodes[5].Pins[1].FloatDefault = 0.5f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Add, 307, 2, true));
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::Multiply, 308, 2, true));
    Graph.Nodes[7].Pins[1].FloatDefault = 256.f;
    Graph.Nodes.Add(Node(EVoxelLandBuiltinNode::DensityOutput, 309, 1, false));
    Link(Graph, 4, 5, 0); Link(Graph, 3, 6, 0); Link(Graph, 5, 6, 1);
    Link(Graph, 6, 7, 0); Link(Graph, 7, 8, 0);
    return Graph;
}
TUniquePtr<FFastNoiseNative> CreateNativeOracle(EVoxelLandBuiltinNode Kind, int32 Index, FString& Error)
{
    if (Kind == EVoxelLandBuiltinNode::Perlin2 || Kind == EVoxelLandBuiltinNode::Perlin3)
    {
        return FFastNoiseNative::CreatePerlin(Error, true);
    }
    if (Kind == EVoxelLandBuiltinNode::Fbm2 || Kind == EVoxelLandBuiltinNode::Fbm3)
    {
        return FFastNoiseNative::CreateFbm(Index == 0 ? 4 : 2, Index == 0 ? 0.375f : 0.75f, Index == 0 ? 2.f : 3.f, Error, true);
    }
    if (Kind == EVoxelLandBuiltinNode::Value2 || Kind == EVoxelLandBuiltinNode::Value3)
    {
        return FFastNoiseNative::CreateValue(Error, true);
    }
    return FFastNoiseNative::CreateSimplex(Error, true);
}
TArray<int16> SampleNativeOracle(EVoxelLandBuiltinNode Kind, bool b3D, int32 Count, uint64 WorldSeed, FString& Error)
{
    return Async(EAsyncExecution::ThreadPool, [Kind, b3D, Count, WorldSeed, &Error]()
    {
        TArray<int16> Result;
        TUniquePtr<FFastNoiseNative> First = CreateNativeOracle(Kind, 0, Error);
        if (!First) { return Result; }
        TUniquePtr<FFastNoiseNative> Second = CreateNativeOracle(Kind, 1, Error);
        if (!Second) { return Result; }
        TArray<float> X0, Y0, Z0, X1, Y1, Z1, A, B;
        X0.SetNumUninitialized(Count); Y0.SetNumUninitialized(Count); Z0.SetNumUninitialized(Count);
        X1.SetNumUninitialized(Count); Y1.SetNumUninitialized(Count); Z1.SetNumUninitialized(Count);
        A.SetNumUninitialized(Count); B.SetNumUninitialized(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            const float PX = float(I - 129) * 106.f;
            const float PY = float((I * 31) % 257 - 128) * 56.f;
            const float PZ = float((I * 11) % 123 - 61) * 40.f;
            X0[I] = PZ / 128.f; Y0[I] = PX / 128.f; Z0[I] = PY / 128.f;
            X1[I] = PZ / 256.f; Y1[I] = PX / 256.f; Z1[I] = PY / 256.f;
        }
        // Fixed seeds independently calculated from the documented SplitMix64 recipe for
        // node GUIDs (0x1234,1,1,304/305), salts 13/67 and these two world seeds.
        const int32 Seed0 = WorldSeed == 314159 ? -1882952887 : 2140967812;
        const int32 Seed1 = WorldSeed == 314159 ? 291379200 : -444965003;
        const bool bFirst = b3D ? First->Sample3D(Count, X0.GetData(), Y0.GetData(), Z0.GetData(), Seed0, A.GetData(), Error) :
            First->Sample2D(Count, X0.GetData(), Y0.GetData(), Seed0, A.GetData(), Error);
        if (!bFirst) { return Result; }
        const bool bSecond = b3D ? Second->Sample3D(Count, X1.GetData(), Y1.GetData(), Z1.GetData(), Seed1, B.GetData(), Error) :
            Second->Sample2D(Count, X1.GetData(), Y1.GetData(), Seed1, B.GetData(), Error);
        if (!bSecond) { return Result; }
        Result.Reserve(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            const float Density = (A[I] + B[I] * 0.5f) * 256.f;
            Result.Add(int16(std::round(Density)));
        }
        return Result;
    }).Get();
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NativeNoiseOracle, "VoxelLand.M3.NativeNoiseOracle", M3GraphFlags)
bool FVoxelLandM3NativeNoiseOracle::RunTest(const FString& Parameters)
{
#if PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
    FVoxelLandZoneProfile Profile; Profile.VoxelSizeCm = 1024.f;
    const EVoxelLandBuiltinNode Kinds[] = {EVoxelLandBuiltinNode::Perlin2, EVoxelLandBuiltinNode::Perlin3,
        EVoxelLandBuiltinNode::Fbm2, EVoxelLandBuiltinNode::Fbm3, EVoxelLandBuiltinNode::Value2,
        EVoxelLandBuiltinNode::Value3, EVoxelLandBuiltinNode::Simplex2, EVoxelLandBuiltinNode::Simplex3};
    for (int32 KindIndex = 0; KindIndex < UE_ARRAY_COUNT(Kinds); ++KindIndex)
    {
        const EVoxelLandBuiltinNode Kind = Kinds[KindIndex];
        const bool b3D = (KindIndex % 2) == 1;
        FVoxelLandProgramDraft Draft; FString Error;
        if (!FVoxelLandGraphCompiler::Compile(NativeOracleGraph(Kind, b3D), Draft, Error))
        {
            AddError(FString::Printf(TEXT("Noise oracle graph %d failed to compile: %s"), int32(Kind), *Error));
            continue;
        }
        TestTrue(TEXT("Two independent native noise slots are compiled"), Draft.Noise.Num() == 2 && Draft.Seeds.Num() == 2);
        for (uint64 WorldSeed : {uint64(314159), uint64(987654321)})
        {
            for (int32 Count : {1, 2, 3, 256})
            {
                FString NativeError;
                const TArray<int16> Expected = SampleNativeOracle(Kind, b3D, Count, WorldSeed, NativeError);
                if (Expected.Num() != Count)
                {
                    AddError(FString::Printf(TEXT("Native reference family %d failed: %s"), int32(Kind), *NativeError));
                    continue;
                }
                for (EVoxelLandVMBackend Backend : {EVoxelLandVMBackend::Scalar, EVoxelLandVMBackend::Auto, EVoxelLandVMBackend::Simd4})
                {
                    const FSampleResult Actual = Sample(Draft, Backend, Profile, Count, WorldSeed);
                    if (Backend == EVoxelLandVMBackend::Simd4 && Actual.Error.Code == EVoxelLandErrorCode::BackendUnavailable)
                    {
                        if (KindIndex == 0 && WorldSeed == 314159 && Count == 256) { AddWarning(TEXT("VM SIMD4 unavailable; native-oracle SIMD selection was not exercised")); }
                        continue;
                    }
                    if (KindIndex == 0 && WorldSeed == 314159 && Count == 256)
                    {
                        AddInfo(FString::Printf(TEXT("Native oracle requested VM %d, selected VM %d, native backend %d"),
                            int32(Backend), int32(Actual.VM), int32(Actual.NoiseBackend)));
                        if (Backend != EVoxelLandVMBackend::Auto)
                        {
                            TestEqual(TEXT("Explicit oracle VM backend selected"), int32(Actual.VM), int32(Backend));
                        }
                    }
                    TestTrue(FString::Printf(TEXT("Native oracle family %d seed %llu count %d backend %d"), int32(Kind), WorldSeed, Count, int32(Backend)),
                        !Actual.Error && Actual.Values == Expected && Actual.NoiseBackend == EVoxelLandNoiseBackend::SSE2);
                }
            }
        }
    }
#else
    AddWarning(TEXT("Native noise oracle requires the supported x64 SSE2 backend; not exercised on this platform"));
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NoiseParity, "VoxelLand.M3.NoiseScalarSimdParity", M3GraphFlags)
bool FVoxelLandM3NoiseParity::RunTest(const FString& Parameters)
{
    const EVoxelLandOpcode Families[] = {EVoxelLandOpcode::Perlin2, EVoxelLandOpcode::Perlin3,
        EVoxelLandOpcode::Fbm2, EVoxelLandOpcode::Fbm3, EVoxelLandOpcode::Value2, EVoxelLandOpcode::Value3,
        EVoxelLandOpcode::Simplex2, EVoxelLandOpcode::Simplex3};
    for (EVoxelLandOpcode Family : Families)
    {
        FVoxelLandProgramDraft Draft = Noise(Family);
        Draft.FormatVersion = 2;
        const FSampleResult FirstSeed = Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), 256, 314159);
        const FSampleResult SecondSeed = Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), 256, 314160);
        if (FirstSeed.Error || SecondSeed.Error)
        {
#if PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
            AddError(FString::Printf(TEXT("Noise family %d seed sampling failed: %s / %s"), int32(Family),
                *FirstSeed.Error.Message, *SecondSeed.Error.Message));
#else
            AddWarning(FString::Printf(TEXT("Noise family %d is unavailable on this platform; parity remains unverified"), int32(Family)));
#endif
            continue;
        }
        if (FirstSeed.Values == SecondSeed.Values)
        {
            int32 Minimum = 32767, Maximum = -32768;
            for (int16 Value : FirstSeed.Values)
            {
                Minimum = FMath::Min(Minimum, int32(Value));
                Maximum = FMath::Max(Maximum, int32(Value));
            }
            AddError(FString::Printf(TEXT("Family %d identical for distinct seeds: count=%d range=[%d,%d] first=%d"),
                int32(Family), FirstSeed.Values.Num(), Minimum, Maximum,
                FirstSeed.Values.IsEmpty() ? 0 : int32(FirstSeed.Values[0])));
        }
        bool bWarnedSimd = false;
        for (int32 Count : {1, 2, 3, 256})
        {
            const FSampleResult Scalar = Sample(Draft, EVoxelLandVMBackend::Scalar, FVoxelLandZoneProfile(), Count);
            const FSampleResult Simd = Sample(Draft, EVoxelLandVMBackend::Simd4, FVoxelLandZoneProfile(), Count);
            if (Scalar.Error)
            {
                AddError(FString::Printf(TEXT("Scalar family %d count %d failed: %s"), int32(Family), Count, *Scalar.Error.Message));
                continue;
            }
            if (Simd.Error.Code == EVoxelLandErrorCode::BackendUnavailable)
            {
                if (!bWarnedSimd)
                {
                    AddWarning(FString::Printf(TEXT("VM SIMD is unavailable for family %d; parity is unverified"), int32(Family)));
                    bWarnedSimd = true;
                }
                continue;
            }
            TestTrue(FString::Printf(TEXT("Family %d count %d scalar/SIMD parity"), int32(Family), Count),
                !Simd.Error && Scalar.Values == Simd.Values);
        }
    }
    return true;
}
#endif
