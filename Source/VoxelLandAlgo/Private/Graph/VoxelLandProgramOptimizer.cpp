#include "Graph/VoxelLandProgramOptimizer.h"
#include "Data/Structs/VoxelLandProgramDraft.h"

namespace
{
// Operands are visited in a fixed order; the callback may rewrite each register.
template<typename F> void VisitOperands(FVoxelLandInstruction& I, const FVoxelLandProgramDraft& Draft, F&& Visit)
{
    switch (I.Op)
    {
        case EVoxelLandOpcode::StoreDensity:
        case EVoxelLandOpcode::Abs:
        case EVoxelLandOpcode::Negate:
        case EVoxelLandOpcode::IntToFloat:
        case EVoxelLandOpcode::BoolNot:
            Visit(I.A); break;
        case EVoxelLandOpcode::Add: case EVoxelLandOpcode::Subtract: case EVoxelLandOpcode::Multiply:
        case EVoxelLandOpcode::Divide: case EVoxelLandOpcode::Min: case EVoxelLandOpcode::Max:
        case EVoxelLandOpcode::Union: case EVoxelLandOpcode::SubtractShape: case EVoxelLandOpcode::Intersect:
        case EVoxelLandOpcode::Perlin2: case EVoxelLandOpcode::Fbm2: case EVoxelLandOpcode::Value2:
        case EVoxelLandOpcode::Simplex2: case EVoxelLandOpcode::Less: case EVoxelLandOpcode::LessEqual:
        case EVoxelLandOpcode::Equal: case EVoxelLandOpcode::BoolAnd: case EVoxelLandOpcode::BoolOr:
        case EVoxelLandOpcode::Power:
            Visit(I.A); Visit(I.B); break;
        case EVoxelLandOpcode::Plane: case EVoxelLandOpcode::Sphere: case EVoxelLandOpcode::Box:
        case EVoxelLandOpcode::Perlin3: case EVoxelLandOpcode::Fbm3: case EVoxelLandOpcode::Value3:
        case EVoxelLandOpcode::Simplex3: case EVoxelLandOpcode::SelectFloat: case EVoxelLandOpcode::SelectInt:
        case EVoxelLandOpcode::SelectBool: case EVoxelLandOpcode::Clamp:
            Visit(I.A); Visit(I.B); Visit(I.C); break;
        case EVoxelLandOpcode::DynamicPlane:
            Visit(I.A); Visit(I.B); Visit(I.C); Visit(I.D); Visit(I.E); Visit(I.F);
            { uint16 Register = uint16(I.Imm); Visit(Register); I.Imm = Register; }
            break;
        case EVoxelLandOpcode::DynamicSphere:
            Visit(I.A); Visit(I.B); Visit(I.C); Visit(I.D); break;
        case EVoxelLandOpcode::DynamicBox:
            Visit(I.A); Visit(I.B); Visit(I.C); Visit(I.D); Visit(I.E); Visit(I.F); break;
        case EVoxelLandOpcode::ExternalOperator:
            Visit(I.A);
            if (Draft.ExternalOperators[I.Imm].Inputs.Num() > 1) { Visit(I.B); }
            if (Draft.ExternalOperators[I.Imm].Inputs.Num() > 2) { Visit(I.C); }
            break;
        default: break;
    }
}

bool IsDiscardable(EVoxelLandOpcode Op)
{
    switch (Op)
    {
        case EVoxelLandOpcode::LoadX: case EVoxelLandOpcode::LoadY: case EVoxelLandOpcode::LoadZ:
        case EVoxelLandOpcode::LoadConstant: case EVoxelLandOpcode::LoadIntConstant: case EVoxelLandOpcode::LoadBoolConstant:
        case EVoxelLandOpcode::LoadFloatParameter: case EVoxelLandOpcode::LoadIntParameter: case EVoxelLandOpcode::LoadBoolParameter:
        case EVoxelLandOpcode::BoolAnd: case EVoxelLandOpcode::BoolOr: case EVoxelLandOpcode::BoolNot:
        case EVoxelLandOpcode::Less: case EVoxelLandOpcode::LessEqual: case EVoxelLandOpcode::Equal:
        case EVoxelLandOpcode::SelectFloat: case EVoxelLandOpcode::SelectInt: case EVoxelLandOpcode::SelectBool:
            return true;
        default: return false;
    }
}

void RemoveUnusedTotalInstructions(FVoxelLandProgramDraft& Draft)
{
    TArray<int32> Uses;
    Uses.Init(0, Draft.RegisterCount);
    TArray<bool> Keep;
    Keep.Init(true, Draft.Instructions.Num());
    for (int32 Index = Draft.Instructions.Num() - 1; Index >= 0; --Index)
    {
        FVoxelLandInstruction& I = Draft.Instructions[Index];
        if (IsDiscardable(I.Op) && Uses[I.Dst] == 0)
        {
            Keep[Index] = false;
            continue;
        }
        VisitOperands(I, Draft, [&Uses](uint16& Operand) { ++Uses[Operand]; });
    }
    TArray<FVoxelLandInstruction> Instructions;
    Instructions.Reserve(Draft.Instructions.Num());
    for (int32 Index = 0; Index < Draft.Instructions.Num(); ++Index)
    {
        if (Keep[Index])
        {
            Instructions.Add(Draft.Instructions[Index]);
        }
    }
    Draft.Instructions = MoveTemp(Instructions);
}

void CompactConstants(FVoxelLandProgramDraft& Draft)
{
    TArray<bool> Used;
    Used.Init(false, Draft.Constants.Num());
    auto Mark = [&Used](uint32 Index) { Used[Index] = true; };
    for (const FVoxelLandInstruction& I : Draft.Instructions)
    {
        int32 Count = I.Op == EVoxelLandOpcode::LoadConstant || I.Op == EVoxelLandOpcode::Sphere ? 1 :
            I.Op == EVoxelLandOpcode::Box ? 3 : I.Op == EVoxelLandOpcode::Plane ? 4 : 0;
        for (int32 Offset = 0; Offset < Count; ++Offset) { Mark(I.Imm + Offset); }
    }
    auto MarkSource = [&Mark](const FVoxelLandValueSource& Source) { if (!Source.bParameter) { Mark(Source.Index); } };
    for (const FVoxelLandNoiseConfig& Noise : Draft.Noise)
    {
        MarkSource(Noise.Frequency); MarkSource(Noise.Octaves); MarkSource(Noise.Gain); MarkSource(Noise.Lacunarity);
    }
    if (Draft.SmoothFilter.bPresent)
    {
        MarkSource(Draft.SmoothFilter.Enabled); MarkSource(Draft.SmoothFilter.Strength);
    }
    TArray<uint32> Remap;
    Remap.SetNum(Draft.Constants.Num());
    TArray<float> Constants;
    for (int32 Index = 0; Index < Draft.Constants.Num(); ++Index)
    {
        if (Used[Index])
        {
            Remap[Index] = uint32(Constants.Add(Draft.Constants[Index]));
        }
    }
    for (FVoxelLandInstruction& I : Draft.Instructions)
    {
        if (I.Op == EVoxelLandOpcode::LoadConstant || I.Op == EVoxelLandOpcode::Sphere || I.Op == EVoxelLandOpcode::Box || I.Op == EVoxelLandOpcode::Plane)
        {
            I.Imm = Remap[I.Imm];
        }
    }
    auto RemapSource = [&Remap](FVoxelLandValueSource& Source) { if (!Source.bParameter) { Source.Index = Remap[Source.Index]; } };
    for (FVoxelLandNoiseConfig& Noise : Draft.Noise)
    {
        RemapSource(Noise.Frequency); RemapSource(Noise.Octaves); RemapSource(Noise.Gain); RemapSource(Noise.Lacunarity);
    }
    if (Draft.SmoothFilter.bPresent)
    {
        RemapSource(Draft.SmoothFilter.Enabled); RemapSource(Draft.SmoothFilter.Strength);
    }
    Draft.Constants = MoveTemp(Constants);
}

bool AllocateRegisters(FVoxelLandProgramDraft& Draft, FString& Error)
{
    TArray<int32> Uses;
    Uses.Init(0, Draft.RegisterCount);
    for (FVoxelLandInstruction& I : Draft.Instructions)
    {
        VisitOperands(I, Draft, [&Uses](uint16& Operand) { ++Uses[Operand]; });
    }
    TArray<int32> Physical;
    Physical.Init(INDEX_NONE, Draft.RegisterCount);
    TArray<bool> Occupied;
    for (FVoxelLandInstruction& I : Draft.Instructions)
    {
        FVoxelLandInstruction Original = I;
        const uint16 VirtualDestination = I.Dst;
        VisitOperands(I, Draft, [&Physical](uint16& Operand) { Operand = uint16(Physical[Operand]); });
        if (I.Op != EVoxelLandOpcode::StoreDensity)
        {
            int32 Register = Occupied.Find(false);
            if (Register == INDEX_NONE)
            {
                if (Occupied.Num() >= 1024)
                {
                    Error = TEXT("Graph exceeds live VM register limit");
                    return false;
                }
                Register = Occupied.Add(true);
            }
            else
            {
                Occupied[Register] = true;
            }
            Physical[VirtualDestination] = Register;
            I.Dst = uint16(Register);
        }
        VisitOperands(Original, Draft, [&Uses, &Physical, &Occupied](uint16& Operand)
        {
            if (--Uses[Operand] == 0)
            {
                Occupied[Physical[Operand]] = false;
            }
        });
        if (I.Op != EVoxelLandOpcode::StoreDensity && Uses[VirtualDestination] == 0)
        {
            Occupied[Physical[VirtualDestination]] = false;
        }
    }
    Draft.RegisterCount = Occupied.Num();
    return true;
}
}

bool FVoxelLandProgramOptimizer::Optimize(FVoxelLandProgramDraft& Draft, FString& Error)
{
    RemoveUnusedTotalInstructions(Draft);
    CompactConstants(Draft);
    return AllocateRegisters(Draft, Error);
}
