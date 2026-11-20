#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM1Enums.h"

struct FVoxelLandError
{
    EVoxelLandErrorCode Code = EVoxelLandErrorCode::None;
    FString Message;
    int32 Instruction = INDEX_NONE;
    FString Parameter;
    explicit operator bool() const { return Code != EVoxelLandErrorCode::None; }
};

struct FVoxelLandChunkKey
{
    FIntVector Coordinate = FIntVector::ZeroValue;
    uint8 LOD = 0;
};

struct FVoxelLandChunkData
{
    static constexpr int32 Cells = 32;
    static constexpr int32 SamplesPerAxis = 35;
    static constexpr int32 SampleCount = 35 * 35 * 35;
    FVoxelLandChunkKey Key;
    TArray<int16> Density; // X fastest, then Y, then Z; units of 1/1024 voxel.
    uint64 Hash = 0;
};

struct FVoxelLandZoneProfile
{
    float VoxelSizeCm = 100.0f;
    TMap<FString, float> FloatOverrides;
    TMap<FString, int32> IntOverrides;
};

struct FVoxelLandParameterSlot
{
    FString Name; // ASCII identifier, matched case-insensitively.
    EVoxelLandParameterType Type = EVoxelLandParameterType::Float;
    float FloatDefault = 0.0f;
    int32 IntDefault = 0;
};

struct FVoxelLandSeedSlot { FGuid NodeId; uint32 Salt = 0; };

// A value is supplied either by a constant or by a parameter of the stated type.
struct FVoxelLandValueSource
{
    bool bParameter = false;
    uint32 Index = 0;
};

struct FVoxelLandNoiseConfig
{
    uint32 SeedSlot = 0;
    FVoxelLandValueSource Frequency; // positive float
    FVoxelLandValueSource Octaves;   // int parameter, or integral float constant, 1..16
    FVoxelLandValueSource Gain;       // float, 0..1
    FVoxelLandValueSource Lacunarity; // float, >=1
};

// A/B/C are source registers. Imm selects a constant, parameter, or noise config.
// Plane uses four constants at Imm (unit normal XYZ, offset); sphere uses radius;
// box uses three positive half extents. Shapes consume A/B/C position registers.
struct FVoxelLandInstruction
{
    EVoxelLandOpcode Op = EVoxelLandOpcode::LoadX;
    uint16 Dst = 0, A = 0, B = 0, C = 0;
    uint32 Imm = 0;
};

struct FVoxelLandProgramDraft
{
    uint32 FormatVersion = 1;
    int32 RegisterCount = 0;
    TArray<FVoxelLandInstruction> Instructions;
    TArray<float> Constants;
    TArray<FVoxelLandParameterSlot> Parameters;
    TArray<FVoxelLandSeedSlot> Seeds;
    TArray<FVoxelLandNoiseConfig> Noise;
};

struct FVoxelLandBackendOptions
{
    EVoxelLandVMBackend VM = EVoxelLandVMBackend::Auto;
    EVoxelLandNoiseBackend Noise = EVoxelLandNoiseBackend::Auto;
};
