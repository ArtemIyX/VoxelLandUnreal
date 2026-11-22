#include "Graph/VoxelLandBuiltinPresentationCatalog.h"
#include "Data/VoxelLandBuiltinCatalog.h"

#define LOCTEXT_NAMESPACE "VoxelLandBuiltinPresentation"

static FText ExplicitTitle(EVoxelLandBuiltinNode Kind)
{
    switch (Kind)
    {
    case EVoxelLandBuiltinNode::DensityOutput: return LOCTEXT("TitleDensityOutput", "Density Output");
	case EVoxelLandBuiltinNode::M4HeightOutput:
		return LOCTEXT("TitleM4HeightOutput", "M4 Height Output");
	case EVoxelLandBuiltinNode::PositionX: return LOCTEXT("TitlePositionX", "Position X");
    case EVoxelLandBuiltinNode::PositionY: return LOCTEXT("TitlePositionY", "Position Y");
    case EVoxelLandBuiltinNode::PositionZ: return LOCTEXT("TitlePositionZ", "Position Z");
    case EVoxelLandBuiltinNode::FloatConstant: return LOCTEXT("TitleFloatConstant", "Float Constant");
    case EVoxelLandBuiltinNode::FloatParameter: return LOCTEXT("TitleFloatParameter", "Float Parameter");
    case EVoxelLandBuiltinNode::Add: return LOCTEXT("TitleAdd", "Add");
    case EVoxelLandBuiltinNode::Subtract: return LOCTEXT("TitleSubtract", "Subtract");
    case EVoxelLandBuiltinNode::Multiply: return LOCTEXT("TitleMultiply", "Multiply");
    case EVoxelLandBuiltinNode::Divide: return LOCTEXT("TitleDivide", "Divide");
    case EVoxelLandBuiltinNode::Min: return LOCTEXT("TitleMin", "Min");
    case EVoxelLandBuiltinNode::Max: return LOCTEXT("TitleMax", "Max");
    case EVoxelLandBuiltinNode::Abs: return LOCTEXT("TitleAbs", "Abs");
    case EVoxelLandBuiltinNode::Negate: return LOCTEXT("TitleNegate", "Negate");
    case EVoxelLandBuiltinNode::Union: return LOCTEXT("TitleUnion", "Union");
    case EVoxelLandBuiltinNode::SubtractShape: return LOCTEXT("TitleSubtractShape", "Subtract Shape");
    case EVoxelLandBuiltinNode::Intersect: return LOCTEXT("TitleIntersect", "Intersect");
    case EVoxelLandBuiltinNode::HeightToDensity: return LOCTEXT("TitleHeightToDensity", "Height to Density");
    case EVoxelLandBuiltinNode::Clamp: return LOCTEXT("TitleClamp", "Clamp");
    case EVoxelLandBuiltinNode::Saturate: return LOCTEXT("TitleSaturate", "Saturate");
    case EVoxelLandBuiltinNode::Lerp: return LOCTEXT("TitleLerp", "Lerp");
    case EVoxelLandBuiltinNode::IntConstant: return LOCTEXT("TitleIntConstant", "Int Constant");
    case EVoxelLandBuiltinNode::BoolConstant: return LOCTEXT("TitleBoolConstant", "Bool Constant");
    case EVoxelLandBuiltinNode::IntParameter: return LOCTEXT("TitleIntParameter", "Int Parameter");
    case EVoxelLandBuiltinNode::BoolParameter: return LOCTEXT("TitleBoolParameter", "Bool Parameter");
    case EVoxelLandBuiltinNode::IntToFloat: return LOCTEXT("TitleIntToFloat", "Int to Float");
    case EVoxelLandBuiltinNode::Less: return LOCTEXT("TitleLess", "Less");
    case EVoxelLandBuiltinNode::LessEqual: return LOCTEXT("TitleLessEqual", "Less Equal");
    case EVoxelLandBuiltinNode::Equal: return LOCTEXT("TitleEqual", "Equal");
    case EVoxelLandBuiltinNode::BoolAnd: return LOCTEXT("TitleBoolAnd", "And");
    case EVoxelLandBuiltinNode::BoolOr: return LOCTEXT("TitleBoolOr", "Or");
    case EVoxelLandBuiltinNode::BoolNot: return LOCTEXT("TitleBoolNot", "Not");
    case EVoxelLandBuiltinNode::SelectFloat: return LOCTEXT("TitleSelectFloat", "Select Float");
    case EVoxelLandBuiltinNode::SelectInt: return LOCTEXT("TitleSelectInt", "Select Int");
    case EVoxelLandBuiltinNode::SelectBool: return LOCTEXT("TitleSelectBool", "Select Bool");
    case EVoxelLandBuiltinNode::Power: return LOCTEXT("TitlePower", "Power");
    case EVoxelLandBuiltinNode::Plane: return LOCTEXT("TitlePlane", "Plane");
    case EVoxelLandBuiltinNode::Sphere: return LOCTEXT("TitleSphere", "Sphere");
    case EVoxelLandBuiltinNode::Box: return LOCTEXT("TitleBox", "Box");
    case EVoxelLandBuiltinNode::Perlin2: return LOCTEXT("TitlePerlin2", "Perlin 2D");
    case EVoxelLandBuiltinNode::Perlin3: return LOCTEXT("TitlePerlin3", "Perlin 3D");
    case EVoxelLandBuiltinNode::Fbm2: return LOCTEXT("TitleFbm2", "fBm 2D");
    case EVoxelLandBuiltinNode::Fbm3: return LOCTEXT("TitleFbm3", "fBm 3D");
    case EVoxelLandBuiltinNode::Value2: return LOCTEXT("TitleValue2", "Value 2D");
    case EVoxelLandBuiltinNode::Value3: return LOCTEXT("TitleValue3", "Value 3D");
    case EVoxelLandBuiltinNode::Simplex2: return LOCTEXT("TitleSimplex2", "Simplex 2D");
    case EVoxelLandBuiltinNode::Simplex3: return LOCTEXT("TitleSimplex3", "Simplex 3D");
    case EVoxelLandBuiltinNode::RerouteFloat: return LOCTEXT("TitleRerouteFloat", "Reroute Float");
    case EVoxelLandBuiltinNode::RerouteInt: return LOCTEXT("TitleRerouteInt", "Reroute Int");
    case EVoxelLandBuiltinNode::RerouteBool: return LOCTEXT("TitleRerouteBool", "Reroute Bool");
    case EVoxelLandBuiltinNode::SmoothDensity: return LOCTEXT("TitleSmoothDensity", "Smooth Density");
    default: return FText::GetEmpty();
    }
}

static FText ExplicitDescription(EVoxelLandBuiltinNode Kind)
{
    switch (Kind)
    {
    case EVoxelLandBuiltinNode::DensityOutput: return LOCTEXT("DescriptionDensityOutput", "Final signed density in centimeters. Negative is solid; positive is empty. A graph requires exactly one output.");
	case EVoxelLandBuiltinNode::M4HeightOutput:
		return LOCTEXT("DescriptionM4HeightOutput",
			"Three normalized control inputs feed their world-config curves, then combine in fixed order with base height before erosion.");
	case EVoxelLandBuiltinNode::PositionX: return LOCTEXT("DescriptionPositionX", "World-space X coordinate in centimeters.");
    case EVoxelLandBuiltinNode::PositionY: return LOCTEXT("DescriptionPositionY", "World-space Y coordinate in centimeters.");
    case EVoxelLandBuiltinNode::PositionZ: return LOCTEXT("DescriptionPositionZ", "World-space Z coordinate in centimeters.");
    case EVoxelLandBuiltinNode::FloatConstant: return LOCTEXT("DescriptionFloatConstant", "A constant floating-point value.");
    case EVoxelLandBuiltinNode::FloatParameter: return LOCTEXT("DescriptionFloatParameter", "Reads a float parameter selected by its stable ID; world settings may override it.");
    case EVoxelLandBuiltinNode::Add: return LOCTEXT("DescriptionAdd", "Adds inputs in their displayed order. Add, remove, or reorder inputs from the context menu.");
    case EVoxelLandBuiltinNode::Subtract: return LOCTEXT("DescriptionSubtract", "Subtracts B from A.");
    case EVoxelLandBuiltinNode::Multiply: return LOCTEXT("DescriptionMultiply", "Multiplies inputs in their displayed order. Add, remove, or reorder inputs from the context menu.");
    case EVoxelLandBuiltinNode::Divide: return LOCTEXT("DescriptionDivide", "Divides A by B. A zero divisor or non-finite result causes an evaluation error.");
    case EVoxelLandBuiltinNode::Min: return LOCTEXT("DescriptionMin", "Returns the smaller of A and B.");
    case EVoxelLandBuiltinNode::Max: return LOCTEXT("DescriptionMax", "Returns the larger of A and B.");
    case EVoxelLandBuiltinNode::Abs: return LOCTEXT("DescriptionAbs", "Returns the absolute value.");
    case EVoxelLandBuiltinNode::Negate: return LOCTEXT("DescriptionNegate", "Negates the input.");
    case EVoxelLandBuiltinNode::Union: return LOCTEXT("DescriptionUnion", "Combines signed density fields with a hard union (minimum).");
    case EVoxelLandBuiltinNode::SubtractShape: return LOCTEXT("DescriptionSubtractShape", "Cuts field B from field A using hard signed-density subtraction.");
    case EVoxelLandBuiltinNode::Intersect: return LOCTEXT("DescriptionIntersect", "Keeps the intersection of two signed density fields (maximum).");
    case EVoxelLandBuiltinNode::HeightToDensity: return LOCTEXT("DescriptionHeightToDensity", "Converts a height in centimeters to density using world Z minus height.");
    case EVoxelLandBuiltinNode::Clamp: return LOCTEXT("DescriptionClamp", "Restricts the value to the inclusive minimum and maximum. Minimum must not exceed maximum.");
    case EVoxelLandBuiltinNode::Saturate: return LOCTEXT("DescriptionSaturate", "Restricts the value to the range 0 to 1.");
    case EVoxelLandBuiltinNode::Lerp: return LOCTEXT("DescriptionLerp", "Interpolates as A + (B - A) * Alpha. Alpha is not implicitly clamped.");
    case EVoxelLandBuiltinNode::IntConstant: return LOCTEXT("DescriptionIntConstant", "A constant signed 32-bit integer.");
    case EVoxelLandBuiltinNode::BoolConstant: return LOCTEXT("DescriptionBoolConstant", "A constant boolean value.");
    case EVoxelLandBuiltinNode::IntParameter: return LOCTEXT("DescriptionIntParameter", "Reads an integer parameter selected by stable ID.");
    case EVoxelLandBuiltinNode::BoolParameter: return LOCTEXT("DescriptionBoolParameter", "Reads a boolean parameter selected by stable ID.");
    case EVoxelLandBuiltinNode::IntToFloat: return LOCTEXT("DescriptionIntToFloat", "Explicitly converts a 32-bit integer to float; values that cannot be represented exactly cause an evaluation error.");
    case EVoxelLandBuiltinNode::Less: return LOCTEXT("DescriptionLess", "Returns true when A is less than B.");
    case EVoxelLandBuiltinNode::LessEqual: return LOCTEXT("DescriptionLessEqual", "Returns true when A is less than or equal to B.");
    case EVoxelLandBuiltinNode::Equal: return LOCTEXT("DescriptionEqual", "Returns true when A equals B.");
    case EVoxelLandBuiltinNode::BoolAnd: return LOCTEXT("DescriptionBoolAnd", "Returns true when both boolean inputs are true.");
    case EVoxelLandBuiltinNode::BoolOr: return LOCTEXT("DescriptionBoolOr", "Returns true when either boolean input is true.");
    case EVoxelLandBuiltinNode::BoolNot: return LOCTEXT("DescriptionBoolNot", "Inverts a boolean value.");
    case EVoxelLandBuiltinNode::SelectFloat: return LOCTEXT("DescriptionSelectFloat", "Returns True or False float input according to Condition.");
    case EVoxelLandBuiltinNode::SelectInt: return LOCTEXT("DescriptionSelectInt", "Returns True or False integer input according to Condition.");
    case EVoxelLandBuiltinNode::SelectBool: return LOCTEXT("DescriptionSelectBool", "Returns True or False boolean input according to Condition.");
    case EVoxelLandBuiltinNode::Power: return LOCTEXT("DescriptionPower", "Raises Base to Exponent. Invalid domains and non-finite results cause an evaluation error.");
    case EVoxelLandBuiltinNode::Plane: return LOCTEXT("DescriptionPlane", "Signed distance to a plane. The normal must be unit length; offset and position are in centimeters.");
    case EVoxelLandBuiltinNode::Sphere: return LOCTEXT("DescriptionSphere", "Signed distance to a sphere centered at the origin; position and positive radius are in centimeters.");
    case EVoxelLandBuiltinNode::Box: return LOCTEXT("DescriptionBox", "Signed distance to an axis-aligned box centered at the origin; positive half extents and position are in centimeters.");
    case EVoxelLandBuiltinNode::Perlin2: return LOCTEXT("DescriptionPerlin2", "Samples independently seeded 2D Perlin noise at X and Y.");
    case EVoxelLandBuiltinNode::Perlin3: return LOCTEXT("DescriptionPerlin3", "Samples independently seeded 3D Perlin noise at X, Y and Z.");
    case EVoxelLandBuiltinNode::Fbm2: return LOCTEXT("DescriptionFbm2", "Samples independently seeded 2D fractal Brownian Perlin noise.");
    case EVoxelLandBuiltinNode::Fbm3: return LOCTEXT("DescriptionFbm3", "Samples independently seeded 3D fractal Brownian Perlin noise.");
    case EVoxelLandBuiltinNode::Value2: return LOCTEXT("DescriptionValue2", "Samples independently seeded 2D value noise.");
    case EVoxelLandBuiltinNode::Value3: return LOCTEXT("DescriptionValue3", "Samples independently seeded 3D value noise.");
    case EVoxelLandBuiltinNode::Simplex2: return LOCTEXT("DescriptionSimplex2", "Samples independently seeded 2D simplex noise.");
    case EVoxelLandBuiltinNode::Simplex3: return LOCTEXT("DescriptionSimplex3", "Samples independently seeded 3D simplex noise.");
    case EVoxelLandBuiltinNode::RerouteFloat: return LOCTEXT("DescriptionRerouteFloat", "Routes a float wire without changing its value.");
    case EVoxelLandBuiltinNode::RerouteInt: return LOCTEXT("DescriptionRerouteInt", "Routes an integer wire without changing its value.");
    case EVoxelLandBuiltinNode::RerouteBool: return LOCTEXT("DescriptionRerouteBool", "Routes a boolean wire without changing its value.");
    case EVoxelLandBuiltinNode::SmoothDensity: return LOCTEXT("DescriptionSmoothDensity", "Applies one-cell density smoothing before Density Output. Enabled and Strength are uniform controls; strength ranges from 0 to 1.");
    default: return FText::GetEmpty();
    }
}

static FText ExplicitCategory(EVoxelLandBuiltinNode Kind)
{
    switch (Kind)
    {
    case EVoxelLandBuiltinNode::DensityOutput:
	case EVoxelLandBuiltinNode::M4HeightOutput:
	case EVoxelLandBuiltinNode::PositionX:
    case EVoxelLandBuiltinNode::PositionY:
    case EVoxelLandBuiltinNode::PositionZ:
    case EVoxelLandBuiltinNode::FloatConstant:
    case EVoxelLandBuiltinNode::FloatParameter:
    case EVoxelLandBuiltinNode::IntConstant:
    case EVoxelLandBuiltinNode::BoolConstant:
    case EVoxelLandBuiltinNode::IntParameter:
    case EVoxelLandBuiltinNode::BoolParameter:
        return LOCTEXT("CategoryInputs", "Inputs and output");
    case EVoxelLandBuiltinNode::Union:
    case EVoxelLandBuiltinNode::SubtractShape:
    case EVoxelLandBuiltinNode::Intersect:
    case EVoxelLandBuiltinNode::HeightToDensity:
        return LOCTEXT("CategoryTerrain", "Terrain and CSG");
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
        return LOCTEXT("CategoryControl", "Typed control");
    case EVoxelLandBuiltinNode::SmoothDensity:
        return LOCTEXT("CategoryFilters", "Density filters");
    case EVoxelLandBuiltinNode::RerouteFloat:
    case EVoxelLandBuiltinNode::RerouteInt:
    case EVoxelLandBuiltinNode::RerouteBool:
        return LOCTEXT("CategoryOrganization", "Organization");
    case EVoxelLandBuiltinNode::Perlin2:
    case EVoxelLandBuiltinNode::Perlin3:
    case EVoxelLandBuiltinNode::Fbm2:
    case EVoxelLandBuiltinNode::Fbm3:
    case EVoxelLandBuiltinNode::Value2:
    case EVoxelLandBuiltinNode::Value3:
    case EVoxelLandBuiltinNode::Simplex2:
    case EVoxelLandBuiltinNode::Simplex3:
        return LOCTEXT("CategoryNoise", "Noise");
    case EVoxelLandBuiltinNode::Plane:
    case EVoxelLandBuiltinNode::Sphere:
    case EVoxelLandBuiltinNode::Box:
        return LOCTEXT("CategoryShapes", "Shapes");
    case EVoxelLandBuiltinNode::Add:
    case EVoxelLandBuiltinNode::Subtract:
    case EVoxelLandBuiltinNode::Multiply:
    case EVoxelLandBuiltinNode::Divide:
    case EVoxelLandBuiltinNode::Min:
    case EVoxelLandBuiltinNode::Max:
    case EVoxelLandBuiltinNode::Abs:
    case EVoxelLandBuiltinNode::Negate:
    case EVoxelLandBuiltinNode::Clamp:
    case EVoxelLandBuiltinNode::Saturate:
    case EVoxelLandBuiltinNode::Lerp:
    case EVoxelLandBuiltinNode::Power:
        return LOCTEXT("CategoryMath", "Math and control");
    default: return FText::GetEmpty();
    }
}

static FLinearColor ExplicitTitleColor(EVoxelLandBuiltinNode Kind);

bool FVoxelLandBuiltinPresentationCatalog::Validate(FString& OutError)
{
    if (!FVoxelLandBuiltinCatalog::Validate(OutError))
    {
        return false;
    }
    TArray<EVoxelLandBuiltinNode> Kinds;
    FVoxelLandBuiltinCatalog::GetKinds(Kinds);
    for (EVoxelLandBuiltinNode Kind : Kinds)
    {
        if (ExplicitTitle(Kind).IsEmpty() || ExplicitDescription(Kind).IsEmpty() || ExplicitCategory(Kind).IsEmpty() || ExplicitTitleColor(Kind).R < 0.f)
        {
            OutError = FString::Printf(TEXT("Missing builtin editor presentation for %u"), uint32(Kind));
            return false;
        }
    }
    return true;
}

FText FVoxelLandBuiltinPresentationCatalog::Title(EVoxelLandBuiltinNode Kind)
{
    const FText Value = ExplicitTitle(Kind);
    return Value.IsEmpty() ? LOCTEXT("UnknownTitle", "Unknown operator") : Value;
}

FText FVoxelLandBuiltinPresentationCatalog::Description(EVoxelLandBuiltinNode Kind)
{
    const FText Value = ExplicitDescription(Kind);
    return Value.IsEmpty() ? LOCTEXT("UnknownDescription", "Unknown operator.") : Value;
}

FText FVoxelLandBuiltinPresentationCatalog::Category(EVoxelLandBuiltinNode Kind)
{
    const FText Value = ExplicitCategory(Kind);
    return Value.IsEmpty() ? LOCTEXT("CategoryMath", "Math and control") : Value;
}

static FLinearColor ExplicitTitleColor(EVoxelLandBuiltinNode Kind)
{
    switch (Kind)
    {
    case EVoxelLandBuiltinNode::DensityOutput:
	case EVoxelLandBuiltinNode::M4HeightOutput:
		return FLinearColor(0.55f, 0.16f, 0.13f);
    case EVoxelLandBuiltinNode::SmoothDensity:
        return FLinearColor(0.15f, 0.38f, 0.46f);
    case EVoxelLandBuiltinNode::Perlin2:
    case EVoxelLandBuiltinNode::Perlin3:
    case EVoxelLandBuiltinNode::Fbm2:
    case EVoxelLandBuiltinNode::Fbm3:
    case EVoxelLandBuiltinNode::Value2:
    case EVoxelLandBuiltinNode::Value3:
    case EVoxelLandBuiltinNode::Simplex2:
    case EVoxelLandBuiltinNode::Simplex3:
        return FLinearColor(0.48f, 0.30f, 0.12f);
    case EVoxelLandBuiltinNode::Plane:
    case EVoxelLandBuiltinNode::Sphere:
    case EVoxelLandBuiltinNode::Box:
    case EVoxelLandBuiltinNode::Union:
    case EVoxelLandBuiltinNode::SubtractShape:
    case EVoxelLandBuiltinNode::Intersect:
        return FLinearColor(0.15f, 0.42f, 0.30f);
    case EVoxelLandBuiltinNode::RerouteFloat:
    case EVoxelLandBuiltinNode::RerouteInt:
    case EVoxelLandBuiltinNode::RerouteBool:
        return FLinearColor(0.25f, 0.25f, 0.25f);
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
        return FLinearColor(0.32f, 0.22f, 0.50f);
    case EVoxelLandBuiltinNode::PositionX:
    case EVoxelLandBuiltinNode::PositionY:
    case EVoxelLandBuiltinNode::PositionZ:
    case EVoxelLandBuiltinNode::FloatConstant:
    case EVoxelLandBuiltinNode::FloatParameter:
    case EVoxelLandBuiltinNode::IntConstant:
    case EVoxelLandBuiltinNode::BoolConstant:
    case EVoxelLandBuiltinNode::IntParameter:
    case EVoxelLandBuiltinNode::BoolParameter:
        return FLinearColor(0.18f, 0.34f, 0.53f);
    case EVoxelLandBuiltinNode::Add:
    case EVoxelLandBuiltinNode::Subtract:
    case EVoxelLandBuiltinNode::Multiply:
    case EVoxelLandBuiltinNode::Divide:
    case EVoxelLandBuiltinNode::Min:
    case EVoxelLandBuiltinNode::Max:
    case EVoxelLandBuiltinNode::Abs:
    case EVoxelLandBuiltinNode::Negate:
    case EVoxelLandBuiltinNode::HeightToDensity:
    case EVoxelLandBuiltinNode::Clamp:
    case EVoxelLandBuiltinNode::Saturate:
    case EVoxelLandBuiltinNode::Lerp:
    case EVoxelLandBuiltinNode::Power:
        return FLinearColor(0.34f, 0.32f, 0.18f);
    default: return FLinearColor(-1.f, -1.f, -1.f);
    }
}

FLinearColor FVoxelLandBuiltinPresentationCatalog::TitleColor(EVoxelLandBuiltinNode Kind)
{
    const FLinearColor Value = ExplicitTitleColor(Kind);
    return Value.R < 0.f ? FLinearColor(0.34f, 0.32f, 0.18f) : Value;
}

namespace
{
bool IsNoise(EVoxelLandBuiltinNode Kind)
{
    switch (Kind)
    {
    case EVoxelLandBuiltinNode::Perlin2:
    case EVoxelLandBuiltinNode::Perlin3:
    case EVoxelLandBuiltinNode::Fbm2:
    case EVoxelLandBuiltinNode::Fbm3:
    case EVoxelLandBuiltinNode::Value2:
    case EVoxelLandBuiltinNode::Value3:
    case EVoxelLandBuiltinNode::Simplex2:
    case EVoxelLandBuiltinNode::Simplex3:
        return true;
    default:
        return false;
    }
}

bool IsShape(EVoxelLandBuiltinNode Kind)
{
    return Kind == EVoxelLandBuiltinNode::Plane || Kind == EVoxelLandBuiltinNode::Sphere || Kind == EVoxelLandBuiltinNode::Box;
}
}

bool FVoxelLandBuiltinPresentationCatalog::TryPinLabel(EVoxelLandBuiltinNode Kind, FName PinName, FText& OutLabel)
{
    OutLabel = FText::GetEmpty();
    if (Kind == EVoxelLandBuiltinNode::HeightToDensity && PinName == TEXT("Value"))
    {
        OutLabel = NSLOCTEXT("VoxelLandEditorNode", "HeightPin", "Height (cm)");
        return true;
    }
    if ((Kind == EVoxelLandBuiltinNode::SmoothDensity || Kind == EVoxelLandBuiltinNode::DensityOutput) && PinName == TEXT("Density"))
    {
        OutLabel = NSLOCTEXT("VoxelLandEditorNode", "DensityPin", "Density (cm)");
        return true;
    }
    if (IsNoise(Kind) && PinName == TEXT("Frequency"))
    {
        OutLabel = NSLOCTEXT("VoxelLandEditorNode", "FrequencyPin", "Frequency (1/cm)");
        return true;
    }
    if (IsShape(Kind) && (PinName == TEXT("Radius") || PinName == TEXT("Offset") || PinName.ToString().StartsWith(TEXT("Extent"))))
    {
        OutLabel = FText::Format(LOCTEXT("PinCentimeters", "{0} (cm)"), FText::FromName(PinName));
        return true;
    }
    return false;
}

FText FVoxelLandBuiltinPresentationCatalog::PinTooltip(EVoxelLandBuiltinNode Kind, FName PinName, FName PinType, bool bOutput)
{
    if (bOutput)
    {
        return FText::Format(LOCTEXT("OutputTooltipFormat", "{0} output. {1}"), FText::FromName(PinType), Description(Kind));
    }
    const FString Name = PinName.ToString();
    if (Name == TEXT("X") || Name == TEXT("Y") || Name == TEXT("Z"))
    {
        return LOCTEXT("PinCoordinates", "Coordinate in centimeters; connect a world position or another coordinate field.");
    }
    if (Name == TEXT("Frequency")) { return LOCTEXT("PinFrequency", "Noise frequency in inverse centimeters; must be positive and uniform."); }
    if (Name == TEXT("Octaves")) { return LOCTEXT("PinOctaves", "Uniform integer octave count from 1 to 16."); }
    if (Name == TEXT("Gain")) { return LOCTEXT("PinGain", "Uniform amplitude multiplier per octave, from 0 to 1."); }
    if (Name == TEXT("Lacunarity")) { return LOCTEXT("PinLacunarity", "Uniform frequency multiplier per octave; at least 1."); }
    if (Name == TEXT("Density")) { return LOCTEXT("PinDensity", "Signed density in centimeters: negative is solid and positive is empty."); }
    if (Name == TEXT("Strength")) { return LOCTEXT("PinStrength", "Uniform filter blend from 0 (original density) to 1 (full smoothing)."); }
    if (Name == TEXT("Enabled")) { return LOCTEXT("PinEnabled", "Uniform boolean controlling whether the filter is applied."); }
    if (Name == TEXT("Radius")) { return LOCTEXT("PinRadius", "Sphere radius in centimeters; must be positive."); }
    if (Name.StartsWith(TEXT("Extent"))) { return LOCTEXT("PinExtent", "Box half extent in centimeters; must be positive."); }
    if (Name.StartsWith(TEXT("Normal"))) { return LOCTEXT("PinNormal", "Plane normal component; the complete normal must have unit length."); }
    if (Name == TEXT("Offset")) { return LOCTEXT("PinOffset", "Plane offset in centimeters."); }
    if (Name == TEXT("Alpha")) { return LOCTEXT("PinAlpha", "Interpolation factor; values outside 0 to 1 extrapolate."); }
    if (Name == TEXT("Condition")) { return LOCTEXT("PinCondition", "Selects the True input when true; otherwise selects False."); }
    if (Kind == EVoxelLandBuiltinNode::HeightToDensity) { return LOCTEXT("PinHeight", "Surface height in centimeters."); }
    return FText::Format(LOCTEXT("InputTooltipFormat", "{0} input. {1}"), FText::FromName(PinType), Description(Kind));
}

#undef LOCTEXT_NAMESPACE
