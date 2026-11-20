#include "Operators/VoxelLandOperatorOutput.h"

FVoxelLandOperatorOutput::FVoxelLandOperatorOutput(EVoxelLandGraphValueType InType, void* InData, int32 InCount)
    : Data(InData), Count(InCount), Type(InType)
{
    bInvalid = !Data || Count < 1 || Count > 256 ||
        (Type != EVoxelLandGraphValueType::Float && Type != EVoxelLandGraphValueType::Int32 && Type != EVoxelLandGraphValueType::Bool);
}
FVoxelLandOperatorOutput::FVoxelLandOperatorOutput(TArrayView<int16> Density)
    : Data(Density.GetData()), Count(Density.Num()), bDensity(true)
{
    bInvalid = !Data || Count < 1 || Count > 256;
}
bool FVoxelLandOperatorOutput::Claim(int32 Index, bool bCorrectType)
{
    if (bInvalid || !bCorrectType || Index < 0 || Index >= Count || (Written[Index / 64] & (uint64(1) << (Index % 64))))
    {
        bInvalid = true;
        return false;
    }
    Written[Index / 64] |= uint64(1) << (Index % 64);
    return true;
}
bool FVoxelLandOperatorOutput::WriteFloat(int32 Index, float Value)
{
    if (!Claim(Index, !bDensity && Type == EVoxelLandGraphValueType::Float && FMath::IsFinite(Value))) { return false; }
    static_cast<float*>(Data)[Index] = Value;
    return true;
}
bool FVoxelLandOperatorOutput::WriteInt(int32 Index, int32 Value)
{
    if (!Claim(Index, !bDensity && Type == EVoxelLandGraphValueType::Int32)) { return false; }
    static_cast<int32*>(Data)[Index] = Value;
    return true;
}
bool FVoxelLandOperatorOutput::WriteBool(int32 Index, int32 Value)
{
    if (!Claim(Index, !bDensity && Type == EVoxelLandGraphValueType::Bool && (Value == 0 || Value == 1))) { return false; }
    static_cast<int32*>(Data)[Index] = Value;
    return true;
}
bool FVoxelLandOperatorOutput::WriteDensity(int32 Index, int16 Value)
{
    if (!Claim(Index, bDensity)) { return false; }
    static_cast<int16*>(Data)[Index] = Value;
    return true;
}
bool FVoxelLandOperatorOutput::WriteFloat4(int32 Index, const float* Values)
{
    if (!Values || Index < 0 || Index > Count - 4) { bInvalid = true; return false; }
    for (int32 Lane = 0; Lane < 4; ++Lane) { if (!WriteFloat(Index + Lane, Values[Lane])) { return false; } }
    return true;
}
bool FVoxelLandOperatorOutput::WriteInt4(int32 Index, const int32* Values)
{
    if (!Values || Index < 0 || Index > Count - 4) { bInvalid = true; return false; }
    for (int32 Lane = 0; Lane < 4; ++Lane) { if (!WriteInt(Index + Lane, Values[Lane])) { return false; } }
    return true;
}
bool FVoxelLandOperatorOutput::WriteBool4(int32 Index, const int32* Values)
{
    if (!Values || Index < 0 || Index > Count - 4) { bInvalid = true; return false; }
    for (int32 Lane = 0; Lane < 4; ++Lane) { if (!WriteBool(Index + Lane, Values[Lane])) { return false; } }
    return true;
}
bool FVoxelLandOperatorOutput::IsComplete(FString& Error) const
{
    if (bInvalid)
    {
        Error = TEXT("Operator made an invalid, duplicate, out-of-range or wrong-type output write");
        return false;
    }
    for (int32 Index = 0; Index < Count; ++Index)
    {
        if ((Written[Index / 64] & (uint64(1) << (Index % 64))) == 0)
        {
            Error = FString::Printf(TEXT("Operator did not write output lane %d"), Index);
            return false;
        }
    }
    return true;
}
