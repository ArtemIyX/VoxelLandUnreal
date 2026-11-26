#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"

class FVoxelLandBuiltinDescriptors final
{
public:
	using EType = EVoxelLandGraphValueType;

	static void AddPin(FVoxelLandBuiltinNodeSpec& Spec, const TCHAR* Name, EType Type,
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

	static void AddBinary(FVoxelLandBuiltinNodeSpec& Spec, EType Type = EType::Float)
	{
		AddPin(Spec, TEXT("A"), Type);
		AddPin(Spec, TEXT("B"), Type);
	}

	static void AddCoordinates(FVoxelLandBuiltinNodeSpec& Spec, bool b3D)
	{
		AddPin(Spec, TEXT("X"), EType::Float);
		AddPin(Spec, TEXT("Y"), EType::Float);
		if (b3D)
		{
			AddPin(Spec, TEXT("Z"), EType::Float);
		}
	}

	static void AddNoise(FVoxelLandBuiltinNodeSpec& Spec, bool b3D)
	{
		Spec.bNoise = true;
		Spec.NoiseDimensions = b3D ? 3 : 2;
		AddCoordinates(Spec, b3D);
		AddPin(Spec, TEXT("Frequency"), EType::Float, TEXT("0.01"), false, true);
		AddPin(Spec, TEXT("Octaves"), EType::Int32, TEXT("3"), false, true);
		AddPin(Spec, TEXT("Gain"), EType::Float, TEXT("0.5"), false, true);
		AddPin(Spec, TEXT("Lacunarity"), EType::Float, TEXT("2"), false, true);
	}

	static bool UsesTypedCompilerDispatch(EVoxelLandBuiltinNode Kind)
	{
		switch (Kind)
		{
			case EVoxelLandBuiltinNode::IntConstant:
			case EVoxelLandBuiltinNode::BoolConstant:
			case EVoxelLandBuiltinNode::IntParameter:
			case EVoxelLandBuiltinNode::BoolParameter:
			case EVoxelLandBuiltinNode::IntToFloat:
			case EVoxelLandBuiltinNode::Less:
			case EVoxelLandBuiltinNode::LessEqual:
			case EVoxelLandBuiltinNode::Equal:
			case EVoxelLandBuiltinNode::BoolAnd:
			case EVoxelLandBuiltinNode::BoolOr:
			case EVoxelLandBuiltinNode::BoolNot:
			case EVoxelLandBuiltinNode::SelectFloat:
			case EVoxelLandBuiltinNode::SelectInt:
			case EVoxelLandBuiltinNode::SelectBool:
			case EVoxelLandBuiltinNode::Power:
				return true;
			default:
				return false;
		}
	}

	static void AddSelect(FVoxelLandBuiltinNodeSpec& Spec, EType Type)
	{
		AddPin(Spec, TEXT("Condition"), EType::Bool, TEXT("false"));
		AddPin(Spec, TEXT("True"), Type);
		AddPin(Spec, TEXT("False"), Type);
	}
};

bool FVoxelLandBuiltinCatalog::Find(
	EVoxelLandBuiltinNode Kind, FVoxelLandBuiltinNodeSpec& OutSpec, int32 VariadicInputs)
{
	OutSpec = FVoxelLandBuiltinNodeSpec();
	FVoxelLandBuiltinNodeSpec Spec;
	Spec.Kind = Kind;
	Spec.bTypedCompilerDispatch = FVoxelLandBuiltinDescriptors::UsesTypedCompilerDispatch(Kind);
	Spec.OperatorId = VoxelLandBuiltinNodeId(Kind);
	FVoxelLandBuiltinDescriptors::EType OutputType = FVoxelLandBuiltinDescriptors::EType::Float;
	bool bHasOutput = true;
	switch (Kind)
	{
		case EVoxelLandBuiltinNode::FloatOutput:
		case EVoxelLandBuiltinNode::IntOutput:
		case EVoxelLandBuiltinNode::BoolOutput:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"),
				Kind == EVoxelLandBuiltinNode::IntOutput    ? FVoxelLandBuiltinDescriptors::EType::Int32
				: Kind == EVoxelLandBuiltinNode::BoolOutput ? FVoxelLandBuiltinDescriptors::EType::Bool
															: FVoxelLandBuiltinDescriptors::EType::Float);
			bHasOutput = false;
			break;
		case EVoxelLandBuiltinNode::Surface:
			OutputType = FVoxelLandBuiltinDescriptors::EType::Surface;
			break;
		case EVoxelLandBuiltinNode::BlendSurfaces:
			FVoxelLandBuiltinDescriptors::AddBinary(Spec, FVoxelLandBuiltinDescriptors::EType::Surface);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Alpha"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("0.5"));
			OutputType = FVoxelLandBuiltinDescriptors::EType::Surface;
			break;
		case EVoxelLandBuiltinNode::SurfaceMaterialOutput:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Surface"), FVoxelLandBuiltinDescriptors::EType::Surface);
			bHasOutput = false;
			break;
		case EVoxelLandBuiltinNode::SurfaceSlope:
		case EVoxelLandBuiltinNode::TerrainSignal:
			break;
		case EVoxelLandBuiltinNode::WorldSignal:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Default"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Required"), FVoxelLandBuiltinDescriptors::EType::Bool);
			break;
		case EVoxelLandBuiltinNode::MaterialRemap:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Lower"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("-0.5"));
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Upper"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("0.5"));
			break;
		case EVoxelLandBuiltinNode::DensityOutput:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Density"), FVoxelLandBuiltinDescriptors::EType::Float);
			bHasOutput = false;
			break;
		case EVoxelLandBuiltinNode::M4HeightOutput:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Continental"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Mountain"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Peaks"), FVoxelLandBuiltinDescriptors::EType::Float);
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
			OutputType = FVoxelLandBuiltinDescriptors::EType::Int32;
			break;
		case EVoxelLandBuiltinNode::BoolParameter:
			Spec.bParameterReference = true;
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
			break;
		case EVoxelLandBuiltinNode::FloatConstant:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Float);
			break;
		case EVoxelLandBuiltinNode::IntConstant:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Int32);
			OutputType = FVoxelLandBuiltinDescriptors::EType::Int32;
			break;
		case EVoxelLandBuiltinNode::BoolConstant:
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Bool, TEXT("false"));
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
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
				FVoxelLandBuiltinDescriptors::AddPin(Spec, *Name, FVoxelLandBuiltinDescriptors::EType::Float,
					Kind == EVoxelLandBuiltinNode::Multiply ? TEXT("1") : TEXT("0"));
			}
			break;
		case EVoxelLandBuiltinNode::Subtract:
		case EVoxelLandBuiltinNode::Divide:
		case EVoxelLandBuiltinNode::Min:
		case EVoxelLandBuiltinNode::Max:
		case EVoxelLandBuiltinNode::Union:
		case EVoxelLandBuiltinNode::SubtractShape:
		case EVoxelLandBuiltinNode::Intersect:
			FVoxelLandBuiltinDescriptors::AddBinary(Spec);
			break;
		case EVoxelLandBuiltinNode::Less:
		case EVoxelLandBuiltinNode::LessEqual:
		case EVoxelLandBuiltinNode::Equal:
			FVoxelLandBuiltinDescriptors::AddBinary(Spec);
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
			break;
		case EVoxelLandBuiltinNode::BoolAnd:
		case EVoxelLandBuiltinNode::BoolOr:
			FVoxelLandBuiltinDescriptors::AddBinary(Spec, FVoxelLandBuiltinDescriptors::EType::Bool);
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
			break;
		case EVoxelLandBuiltinNode::BoolNot:
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Bool, TEXT("false"));
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
			break;
		case EVoxelLandBuiltinNode::Abs:
		case EVoxelLandBuiltinNode::Negate:
		case EVoxelLandBuiltinNode::HeightToDensity:
		case EVoxelLandBuiltinNode::Saturate:
		case EVoxelLandBuiltinNode::RerouteFloat:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Float);
			break;
		case EVoxelLandBuiltinNode::IntToFloat:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Int32);
			break;
		case EVoxelLandBuiltinNode::RerouteInt:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Int32);
			OutputType = FVoxelLandBuiltinDescriptors::EType::Int32;
			break;
		case EVoxelLandBuiltinNode::RerouteBool:
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Bool, TEXT("false"));
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
			break;
		case EVoxelLandBuiltinNode::Power:
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Base"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("1"));
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Exponent"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("1"));
			break;
		case EVoxelLandBuiltinNode::Clamp:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Value"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Min"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Max"), FVoxelLandBuiltinDescriptors::EType::Float);
			break;
		case EVoxelLandBuiltinNode::Lerp:
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("A"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("B"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Alpha"), FVoxelLandBuiltinDescriptors::EType::Float);
			break;
		case EVoxelLandBuiltinNode::SelectFloat:
			FVoxelLandBuiltinDescriptors::AddSelect(Spec, FVoxelLandBuiltinDescriptors::EType::Float);
			break;
		case EVoxelLandBuiltinNode::SelectInt:
			FVoxelLandBuiltinDescriptors::AddSelect(Spec, FVoxelLandBuiltinDescriptors::EType::Int32);
			OutputType = FVoxelLandBuiltinDescriptors::EType::Int32;
			break;
		case EVoxelLandBuiltinNode::SelectBool:
			FVoxelLandBuiltinDescriptors::AddSelect(Spec, FVoxelLandBuiltinDescriptors::EType::Bool);
			OutputType = FVoxelLandBuiltinDescriptors::EType::Bool;
			break;
		case EVoxelLandBuiltinNode::Plane:
			FVoxelLandBuiltinDescriptors::AddCoordinates(Spec, true);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Normal X"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Normal Y"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Normal Z"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("1"));
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Offset"), FVoxelLandBuiltinDescriptors::EType::Float);
			break;
		case EVoxelLandBuiltinNode::Sphere:
			FVoxelLandBuiltinDescriptors::AddCoordinates(Spec, true);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Radius"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("100"));
			break;
		case EVoxelLandBuiltinNode::Box:
			FVoxelLandBuiltinDescriptors::AddCoordinates(Spec, true);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Extent X"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("100"));
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Extent Y"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("100"));
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Extent Z"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("100"));
			break;
		case EVoxelLandBuiltinNode::WorldNoise2D:
		case EVoxelLandBuiltinNode::Perlin2:
		case EVoxelLandBuiltinNode::Fbm2:
		case EVoxelLandBuiltinNode::Value2:
		case EVoxelLandBuiltinNode::Simplex2:
			FVoxelLandBuiltinDescriptors::AddNoise(Spec, false);
			break;
		case EVoxelLandBuiltinNode::Perlin3:
		case EVoxelLandBuiltinNode::Fbm3:
		case EVoxelLandBuiltinNode::Value3:
		case EVoxelLandBuiltinNode::Simplex3:
			FVoxelLandBuiltinDescriptors::AddNoise(Spec, true);
			break;
		case EVoxelLandBuiltinNode::SmoothDensity:
			Spec.Stage = EVoxelLandOperatorStage::OneCellFilter;
			FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Density"), FVoxelLandBuiltinDescriptors::EType::Float);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Enabled"), FVoxelLandBuiltinDescriptors::EType::Bool, TEXT("true"), false, true);
			FVoxelLandBuiltinDescriptors::AddPin(
				Spec, TEXT("Strength"), FVoxelLandBuiltinDescriptors::EType::Float, TEXT("1"), false, true);
			break;
		default:
			return false;
	}
	if (bHasOutput)
	{
		// Output pins have no authored input default.
		FVoxelLandBuiltinDescriptors::AddPin(Spec, TEXT("Result"), OutputType, TEXT(""), true);
	}
	OutSpec = MoveTemp(Spec);
	return true;
}

void FVoxelLandBuiltinCatalog::GetKinds(TArray<EVoxelLandBuiltinNode>& OutKinds)
{
	static constexpr EVoxelLandBuiltinNode Kinds[] = {
#define VOXELLAND_BUILTIN_KIND(Name, Value) EVoxelLandBuiltinNode::Name,
		VOXELLAND_BUILTIN_NODE_LIST(VOXELLAND_BUILTIN_KIND)
#undef VOXELLAND_BUILTIN_KIND
	};
	OutKinds.Reset(UE_ARRAY_COUNT(Kinds));
	OutKinds.Append(Kinds, UE_ARRAY_COUNT(Kinds));
}

class FVoxelLandBuiltinCatalogLookup final
{
public:
	static bool ValidateBuiltinPins(const FVoxelLandBuiltinNodeSpec& Spec, FString& Error);
	static bool ValidateBuiltinRoles(const FVoxelLandBuiltinNodeSpec& Spec, FString& Error);
};

bool FVoxelLandBuiltinCatalogLookup::ValidateBuiltinPins(const FVoxelLandBuiltinNodeSpec& Spec, FString& Error)
{
	TSet<FName> Names;
	int32 Outputs = 0;
	for (const FVoxelLandBuiltinPinSpec& Pin : Spec.Pins)
	{
		if (Pin.Name.IsEmpty() || Names.Contains(FName(*Pin.Name))
			|| (Pin.Type != EVoxelLandGraphValueType::Float && Pin.Type != EVoxelLandGraphValueType::Int32
				&& Pin.Type != EVoxelLandGraphValueType::Bool && Pin.Type != EVoxelLandGraphValueType::Surface)
			|| (Pin.bOutput && (Pin.bUniform || !Pin.DefaultLiteral.IsEmpty())))
		{
			Error = TEXT("invalid or duplicate pin definition");
			return false;
		}
		Names.Add(FName(*Pin.Name));
		Outputs += Pin.bOutput ? 1 : 0;
		if (!Pin.bOutput)
		{
			float FloatValue = 0.f;
			int32 IntValue = 0;
			const bool bValidDefault =
				Pin.Type == EVoxelLandGraphValueType::Float
					? LexTryParseString(FloatValue, *Pin.DefaultLiteral) && FMath::IsFinite(FloatValue)
				: Pin.Type == EVoxelLandGraphValueType::Int32
					? LexTryParseString(IntValue, *Pin.DefaultLiteral)
					: (Pin.DefaultLiteral == TEXT("true") || Pin.DefaultLiteral == TEXT("false")
						  || Pin.DefaultLiteral == TEXT("0") || Pin.DefaultLiteral == TEXT("1"));
			if (!bValidDefault)
			{
				Error = TEXT("invalid pin default literal");
				return false;
			}
		}
	}
	const int32 ExpectedOutputs =
		(Spec.Kind == EVoxelLandBuiltinNode::DensityOutput || Spec.Kind == EVoxelLandBuiltinNode::M4HeightOutput
			|| Spec.Kind == EVoxelLandBuiltinNode::FloatOutput || Spec.Kind == EVoxelLandBuiltinNode::IntOutput
			|| Spec.Kind == EVoxelLandBuiltinNode::BoolOutput
			|| Spec.Kind == EVoxelLandBuiltinNode::SurfaceMaterialOutput)
			? 0
			: 1;
	if (Outputs != ExpectedOutputs || (Outputs == 1 && !Spec.Pins.Last().bOutput))
	{
		Error = TEXT("invalid output pin definition");
		return false;
	}
	return true;
}

bool FVoxelLandBuiltinCatalogLookup::ValidateBuiltinRoles(const FVoxelLandBuiltinNodeSpec& Spec, FString& Error)
{
	if ((Spec.MinVariadicInputs == 0) != (Spec.MaxVariadicInputs == 0)
		|| (Spec.MinVariadicInputs != 0
			&& (Spec.MinVariadicInputs < 1 || Spec.MaxVariadicInputs < Spec.MinVariadicInputs)))
	{
		Error = TEXT("invalid variadic bounds");
		return false;
	}
	if (Spec.bNoise != (Spec.NoiseDimensions == 2 || Spec.NoiseDimensions == 3))
	{
		Error = TEXT("invalid noise dimensions");
		return false;
	}
	if (Spec.bNoise)
	{
		if (Spec.Pins.Num() != Spec.NoiseDimensions + 5)
		{
			Error = TEXT("invalid noise pin count");
			return false;
		}
		for (int32 Index = 0; Index < Spec.Pins.Num(); ++Index)
		{
			const bool bExpectedUniform = Index >= Spec.NoiseDimensions && Index < Spec.NoiseDimensions + 4;
			if (Spec.Pins[Index].bUniform != bExpectedUniform)
			{
				Error = TEXT("invalid noise uniform pin roles");
				return false;
			}
		}
	}
	else if (Spec.NoiseDimensions != 0)
	{
		Error = TEXT("non-noise builtin has noise dimensions");
		return false;
	}
	if (!Spec.bNoise)
	{
		for (int32 Index = 0; Index < Spec.Pins.Num(); ++Index)
		{
			const bool bExpectedUniform =
				Spec.Kind == EVoxelLandBuiltinNode::SmoothDensity && (Index == 1 || Index == 2);
			if (Spec.Pins[Index].bUniform != bExpectedUniform)
			{
				Error = TEXT("invalid builtin uniform pin roles");
				return false;
			}
		}
	}
	return true;
}

bool FVoxelLandBuiltinCatalog::Validate(FString& OutError)
{
	OutError.Reset();
	TArray<EVoxelLandBuiltinNode> Kinds;
	GetKinds(Kinds);
	TSet<uint32> Values;
	TSet<FGuid> Ids;
	for (EVoxelLandBuiltinNode Kind : Kinds)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		FString Detail;
		if (Values.Contains(uint32(Kind)) || !Find(Kind, Spec) || Spec.Kind != Kind
			|| Spec.OperatorId != VoxelLandBuiltinNodeId(Kind) || Ids.Contains(Spec.OperatorId)
			|| !Spec.OperatorId.IsValid() || Spec.OperatorVersion < 1
			|| !FVoxelLandBuiltinCatalogLookup::ValidateBuiltinPins(Spec, Detail)
			|| !FVoxelLandBuiltinCatalogLookup::ValidateBuiltinRoles(Spec, Detail))
		{
			OutError = FString::Printf(TEXT("Invalid builtin catalog entry %u: %s"), uint32(Kind),
				Detail.IsEmpty() ? TEXT("missing, duplicate or invalid identity") : *Detail);
			return false;
		}
		Values.Add(uint32(Kind));
		Ids.Add(Spec.OperatorId);
		if (Spec.MinVariadicInputs != 0)
		{
			FVoxelLandBuiltinNodeSpec MinSpec;
			FVoxelLandBuiltinNodeSpec MaxSpec;
			if (!Find(Kind, MinSpec, Spec.MinVariadicInputs) || !Find(Kind, MaxSpec, Spec.MaxVariadicInputs)
				|| MinSpec.Pins.Num() != Spec.MinVariadicInputs + 1 || MaxSpec.Pins.Num() != Spec.MaxVariadicInputs + 1
				|| !FVoxelLandBuiltinCatalogLookup::ValidateBuiltinPins(MinSpec, Detail)
				|| !FVoxelLandBuiltinCatalogLookup::ValidateBuiltinPins(MaxSpec, Detail))
			{
				OutError = FString::Printf(TEXT("Invalid builtin catalog variadic entry %u"), uint32(Kind));
				return false;
			}
		}
	}
	return true;
}