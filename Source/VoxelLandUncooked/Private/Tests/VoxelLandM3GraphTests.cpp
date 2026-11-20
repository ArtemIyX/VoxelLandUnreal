#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandM1Fixtures.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Structs/VoxelLandM1SampleResult.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Async/Async.h"
#include "Algo/Reverse.h"
#include "Operators/VoxelLandSquareOperator.h"
#include "Operators/VoxelLandOperator.h"
#include <cfloat>
#include "Misc/AutomationTest.h"
#include "Generation/VoxelLandPower.h"
#include <cmath>

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
using namespace VoxelLandM1Fixtures;
using VoxelLandM1TestData::FSampleResult;

TAtomic<int32> MismatchedSimdCalls(0);

class FMismatchedSimdWorker final : public IVoxelLandOperatorWorker
{
public:
    bool Evaluate(TConstArrayView<const void*> Inputs, void* Output, int32 Count, FString& Error) override
    {
        if (Inputs.Num() != 1 || !Inputs[0] || !Output || Count < 1 || Count > 256)
        {
            Error = TEXT("Invalid mismatch-provider batch");
            return false;
        }
        const float* Input = static_cast<const float*>(Inputs[0]);
        float* Result = static_cast<float*>(Output);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Result[Index] = Input[Index];
        }
        return true;
    }
    bool EvaluateSimd(TConstArrayView<const void*> Inputs, void* Output, int32 Count, FString& Error) override
    {
        ++MismatchedSimdCalls;
        if (!Evaluate(Inputs, Output, Count, Error))
        {
            return false;
        }
        static_cast<float*>(Output)[0] += 1.f;
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
            Points.Emplace(float(I - 129) * 13.25f, float((I * 31) % 257 - 128) * 7.f, float((I * 11) % 123 - 61) * 5.f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3CompilerCodec, "VoxelLand.M3.CompilerAndSerialization", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3TypedDomains, "VoxelLand.M3.TypedAndDomains", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3Power, "VoxelLand.M3.Power", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3BoundsAndOutputs, "VoxelLand.M3.InputBoundsAndOutputs", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3FilterCompilation, "VoxelLand.M3.FilterCompilation", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NoiseGraph, "VoxelLand.M3.NoiseGraphLowering", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3MixedNoise, "VoxelLand.M3.MixedNoiseParameters", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3ExternalProvider, "VoxelLand.M3.ExternalProvider", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3MismatchedSimd, "VoxelLand.M3.ExternalSimdFallback", Flags)
bool FVoxelLandM3MismatchedSimd::RunTest(const FString& Parameters)
{
    FString Error;
    TSharedRef<FMismatchedSimdProvider, ESPMode::ThreadSafe> Provider = MakeShared<FMismatchedSimdProvider, ESPMode::ThreadSafe>();
    if (!FVoxelLandOperatorRegistry::Register(FName(TEXT("VoxelLandUncooked")), Provider, Error))
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
        if (Result == 1)
        {
            AddWarning(TEXT("VM SIMD is unavailable; persistent fallback was not exercised"));
        }
        else
        {
            TestEqual(TEXT("SIMD mismatch disables the kernel for subsequent batches on the same worker"), Result, 2);
        }
    }
    TestTrue(TEXT("Mismatch provider unregisters after workers complete"),
        FVoxelLandOperatorRegistry::Unregister(Provider->Id(), Provider->Version(), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NoiseParity, "VoxelLand.M3.NoiseScalarSimdParity", Flags)
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
        TestTrue(FString::Printf(TEXT("Family %d changes with the world seed"), int32(Family)), FirstSeed.Values != SecondSeed.Values);
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
