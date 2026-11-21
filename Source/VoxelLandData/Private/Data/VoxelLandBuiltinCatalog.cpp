#include "Data/VoxelLandBuiltinCatalog.h"

namespace
{
using EType = EVoxelLandGraphValueType;

void AddPin(FVoxelLandBuiltinNodeSpec& Spec, const TCHAR* Name, EType Type,
    const TCHAR* DefaultLiteral = TEXT("0"), bool bOutput = false, bool bUniform = false)
{
    FVoxelLandBuiltinPinSpec Pin;
    Pin.Name = Name;
    Pin.Type = Type;
    Pin.DefaultLiteral = DefaultLiteral;
    Pin.bOutput = bOutput;
    Pin.bUniform = bUniform;
    Spec.Pins.Add(MoveTemp(Pin));
}

void AddBinary(FVoxelLandBuiltinNodeSpec& Spec, EType Type = EType::Float)
{
    AddPin(Spec, TEXT("A"), Type);
    AddPin(Spec, TEXT("B"), Type);
}

void AddCoordinates(FVoxelLandBuiltinNodeSpec& Spec, bool b3D)
{
    AddPin(Spec, TEXT("X"), EType::Float);
    AddPin(Spec, TEXT("Y"), EType::Float);
    if (b3D)
    {
        AddPin(Spec, TEXT("Z"), EType::Float);
    }
}

void AddNoise(FVoxelLandBuiltinNodeSpec& Spec, bool b3D)
{
    Spec.bNoise = true;
    AddCoordinates(Spec, b3D);
    AddPin(Spec, TEXT("Frequency"), EType::Float, TEXT("0.01"), false, true);
    AddPin(Spec, TEXT("Octaves"), EType::Int32, TEXT("3"), false, true);
    AddPin(Spec, TEXT("Gain"), EType::Float, TEXT("0.5"), false, true);
    AddPin(Spec, TEXT("Lacunarity"), EType::Float, TEXT("2"), false, true);
}

void AddSelect(FVoxelLandBuiltinNodeSpec& Spec, EType Type)
{
    AddPin(Spec, TEXT("Condition"), EType::Bool, TEXT("false"));
    AddPin(Spec, TEXT("True"), Type);
    AddPin(Spec, TEXT("False"), Type);
}
}

bool FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode Kind, FVoxelLandBuiltinNodeSpec& OutSpec, int32 VariadicInputs)
{
    OutSpec = FVoxelLandBuiltinNodeSpec();
    FVoxelLandBuiltinNodeSpec Spec;
    Spec.Kind = Kind;
    Spec.OperatorId = VoxelLandBuiltinNodeId(Kind);
    EType OutputType = EType::Float;
    bool bHasOutput = true;
    switch (Kind)
    {
    case EVoxelLandBuiltinNode::DensityOutput:
        AddPin(Spec, TEXT("Density"), EType::Float);
        bHasOutput = false;
        break;
    case EVoxelLandBuiltinNode::PositionX:
    case EVoxelLandBuiltinNode::PositionY:
    case EVoxelLandBuiltinNode::PositionZ:
        break;
    case EVoxelLandBuiltinNode::FloatParameter:
        Spec.bParameterReference = true;
        break;
    case EVoxelLandBuiltinNode::IntParameter:
        Spec.bParameterReference = true;
        OutputType = EType::Int32;
        break;
    case EVoxelLandBuiltinNode::BoolParameter:
        Spec.bParameterReference = true;
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::FloatConstant:
        AddPin(Spec, TEXT("Value"), EType::Float);
        break;
    case EVoxelLandBuiltinNode::IntConstant:
        AddPin(Spec, TEXT("Value"), EType::Int32);
        OutputType = EType::Int32;
        break;
    case EVoxelLandBuiltinNode::BoolConstant:
        AddPin(Spec, TEXT("Value"), EType::Bool, TEXT("false"));
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::Add:
    case EVoxelLandBuiltinNode::Multiply:
        Spec.MinVariadicInputs = 2;
        Spec.MaxVariadicInputs = 32;
        if (VariadicInputs < Spec.MinVariadicInputs || VariadicInputs > Spec.MaxVariadicInputs)
        {
            return false;
        }
        for (int32 Index = 0; Index < VariadicInputs; ++Index)
        {
            const FString Name = FString::Printf(TEXT("Input %d"), Index + 1);
            AddPin(Spec, *Name, EType::Float, Kind == EVoxelLandBuiltinNode::Multiply ? TEXT("1") : TEXT("0"));
        }
        break;
    case EVoxelLandBuiltinNode::Subtract:
    case EVoxelLandBuiltinNode::Divide:
    case EVoxelLandBuiltinNode::Min:
    case EVoxelLandBuiltinNode::Max:
    case EVoxelLandBuiltinNode::Union:
    case EVoxelLandBuiltinNode::SubtractShape:
    case EVoxelLandBuiltinNode::Intersect:
        AddBinary(Spec);
        break;
    case EVoxelLandBuiltinNode::Less:
    case EVoxelLandBuiltinNode::LessEqual:
    case EVoxelLandBuiltinNode::Equal:
        AddBinary(Spec);
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::BoolAnd:
    case EVoxelLandBuiltinNode::BoolOr:
        AddBinary(Spec, EType::Bool);
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::BoolNot:
        AddPin(Spec, TEXT("Value"), EType::Bool, TEXT("false"));
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::Abs:
    case EVoxelLandBuiltinNode::Negate:
    case EVoxelLandBuiltinNode::HeightToDensity:
    case EVoxelLandBuiltinNode::Saturate:
    case EVoxelLandBuiltinNode::RerouteFloat:
        AddPin(Spec, TEXT("Value"), EType::Float);
        break;
    case EVoxelLandBuiltinNode::IntToFloat:
        AddPin(Spec, TEXT("Value"), EType::Int32);
        break;
    case EVoxelLandBuiltinNode::RerouteInt:
        AddPin(Spec, TEXT("Value"), EType::Int32);
        OutputType = EType::Int32;
        break;
    case EVoxelLandBuiltinNode::RerouteBool:
        AddPin(Spec, TEXT("Value"), EType::Bool, TEXT("false"));
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::Power:
        AddPin(Spec, TEXT("Base"), EType::Float, TEXT("1"));
        AddPin(Spec, TEXT("Exponent"), EType::Float, TEXT("1"));
        break;
    case EVoxelLandBuiltinNode::Clamp:
        AddPin(Spec, TEXT("Value"), EType::Float);
        AddPin(Spec, TEXT("Min"), EType::Float);
        AddPin(Spec, TEXT("Max"), EType::Float);
        break;
    case EVoxelLandBuiltinNode::Lerp:
        AddPin(Spec, TEXT("A"), EType::Float);
        AddPin(Spec, TEXT("B"), EType::Float);
        AddPin(Spec, TEXT("Alpha"), EType::Float);
        break;
    case EVoxelLandBuiltinNode::SelectFloat:
        AddSelect(Spec, EType::Float);
        break;
    case EVoxelLandBuiltinNode::SelectInt:
        AddSelect(Spec, EType::Int32);
        OutputType = EType::Int32;
        break;
    case EVoxelLandBuiltinNode::SelectBool:
        AddSelect(Spec, EType::Bool);
        OutputType = EType::Bool;
        break;
    case EVoxelLandBuiltinNode::Plane:
        AddCoordinates(Spec, true);
        AddPin(Spec, TEXT("Normal X"), EType::Float);
        AddPin(Spec, TEXT("Normal Y"), EType::Float);
        AddPin(Spec, TEXT("Normal Z"), EType::Float, TEXT("1"));
        AddPin(Spec, TEXT("Offset"), EType::Float);
        break;
    case EVoxelLandBuiltinNode::Sphere:
        AddCoordinates(Spec, true);
        AddPin(Spec, TEXT("Radius"), EType::Float, TEXT("100"));
        break;
    case EVoxelLandBuiltinNode::Box:
        AddCoordinates(Spec, true);
        AddPin(Spec, TEXT("Extent X"), EType::Float, TEXT("100"));
        AddPin(Spec, TEXT("Extent Y"), EType::Float, TEXT("100"));
        AddPin(Spec, TEXT("Extent Z"), EType::Float, TEXT("100"));
        break;
    case EVoxelLandBuiltinNode::Perlin2:
    case EVoxelLandBuiltinNode::Fbm2:
    case EVoxelLandBuiltinNode::Value2:
    case EVoxelLandBuiltinNode::Simplex2:
        AddNoise(Spec, false);
        break;
    case EVoxelLandBuiltinNode::Perlin3:
    case EVoxelLandBuiltinNode::Fbm3:
    case EVoxelLandBuiltinNode::Value3:
    case EVoxelLandBuiltinNode::Simplex3:
        AddNoise(Spec, true);
        break;
    case EVoxelLandBuiltinNode::SmoothDensity:
        Spec.Stage = EVoxelLandOperatorStage::OneCellFilter;
        AddPin(Spec, TEXT("Density"), EType::Float);
        AddPin(Spec, TEXT("Enabled"), EType::Bool, TEXT("true"), false, true);
        AddPin(Spec, TEXT("Strength"), EType::Float, TEXT("1"), false, true);
        break;
    default:
        return false;
    }
    if (bHasOutput)
    {
        // Output pins have no authored input default.
        AddPin(Spec, TEXT("Result"), OutputType, TEXT(""), true);
    }
    OutSpec = MoveTemp(Spec);
    return true;
}

void FVoxelLandBuiltinCatalog::GetKinds(TArray<EVoxelLandBuiltinNode>& OutKinds)
{
    static constexpr EVoxelLandBuiltinNode Kinds[] = {
        EVoxelLandBuiltinNode::DensityOutput,
        EVoxelLandBuiltinNode::PositionX,
        EVoxelLandBuiltinNode::PositionY,
        EVoxelLandBuiltinNode::PositionZ,
        EVoxelLandBuiltinNode::FloatConstant,
        EVoxelLandBuiltinNode::FloatParameter,
        EVoxelLandBuiltinNode::Add,
        EVoxelLandBuiltinNode::Subtract,
        EVoxelLandBuiltinNode::Multiply,
        EVoxelLandBuiltinNode::Divide,
        EVoxelLandBuiltinNode::Min,
        EVoxelLandBuiltinNode::Max,
        EVoxelLandBuiltinNode::Abs,
        EVoxelLandBuiltinNode::Negate,
        EVoxelLandBuiltinNode::Union,
        EVoxelLandBuiltinNode::SubtractShape,
        EVoxelLandBuiltinNode::Intersect,
        EVoxelLandBuiltinNode::HeightToDensity,
        EVoxelLandBuiltinNode::Clamp,
        EVoxelLandBuiltinNode::Saturate,
        EVoxelLandBuiltinNode::Lerp,
        EVoxelLandBuiltinNode::IntConstant,
        EVoxelLandBuiltinNode::BoolConstant,
        EVoxelLandBuiltinNode::IntParameter,
        EVoxelLandBuiltinNode::BoolParameter,
        EVoxelLandBuiltinNode::IntToFloat,
        EVoxelLandBuiltinNode::Less,
        EVoxelLandBuiltinNode::LessEqual,
        EVoxelLandBuiltinNode::Equal,
        EVoxelLandBuiltinNode::BoolAnd,
        EVoxelLandBuiltinNode::BoolOr,
        EVoxelLandBuiltinNode::BoolNot,
        EVoxelLandBuiltinNode::SelectFloat,
        EVoxelLandBuiltinNode::SelectInt,
        EVoxelLandBuiltinNode::SelectBool,
        EVoxelLandBuiltinNode::Power,
        EVoxelLandBuiltinNode::Plane,
        EVoxelLandBuiltinNode::Sphere,
        EVoxelLandBuiltinNode::Box,
        EVoxelLandBuiltinNode::Perlin2,
        EVoxelLandBuiltinNode::Perlin3,
        EVoxelLandBuiltinNode::Fbm2,
        EVoxelLandBuiltinNode::Fbm3,
        EVoxelLandBuiltinNode::Value2,
        EVoxelLandBuiltinNode::Value3,
        EVoxelLandBuiltinNode::Simplex2,
        EVoxelLandBuiltinNode::Simplex3,
        EVoxelLandBuiltinNode::RerouteFloat,
        EVoxelLandBuiltinNode::RerouteInt,
        EVoxelLandBuiltinNode::RerouteBool,
        EVoxelLandBuiltinNode::SmoothDensity,
    };
    OutKinds.Reset(UE_ARRAY_COUNT(Kinds));
    OutKinds.Append(Kinds, UE_ARRAY_COUNT(Kinds));
}
