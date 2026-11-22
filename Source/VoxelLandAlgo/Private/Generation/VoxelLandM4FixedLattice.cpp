#include "Generation/VoxelLandM4FixedLattice.h"

#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4Control.h"
#include <cmath>
#include <limits>

namespace VoxelLandM4FixedLattice
{
namespace
{
constexpr int64 Q = int64(1) << 24;
constexpr int64 Limit = int64(64) * Q;

enum class EKind : uint8
{
	Unknown,
	X,
	Y,
	Value
};

FVoxelLandError Fail(const TCHAR* Message, int32 Instruction = INDEX_NONE,
	EVoxelLandErrorCode Code = EVoxelLandErrorCode::InvalidProgram)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	Error.Instruction = Instruction;
	return Error;
}

bool Quantize(float Value, int64& Out)
{
	if (!FMath::IsFinite(Value) || FMath::Abs(double(Value)) > 64.0)
	{
		return false;
	}
	const double Scaled = double(Value) * double(Q);
	Out = Scaled < 0 ? -int64(std::floor(-Scaled + 0.5)) : int64(std::floor(Scaled + 0.5));
	return true;
}

int64 RoundRatio(int64 Numerator, int64 Denominator)
{
	const bool bNegative = Numerator < 0;
	const int64 Magnitude = bNegative ? -Numerator : Numerator;
	int64 Result = Magnitude / Denominator;
	const int64 Remainder = Magnitude % Denominator;
	if (Remainder && Remainder >= Denominator - Remainder)
	{
		++Result;
	}
	return bNegative ? -Result : Result;
}

int64 Mul(int64 A, int64 B)
{
	return RoundRatio(A * B, Q);
}

uint64 Mix(uint64 X)
{
	X += 0x9e3779b97f4a7c15ull;
	X = (X ^ (X >> 30)) * 0xbf58476d1ce4e5b9ull;
	X = (X ^ (X >> 27)) * 0x94d049bb133111ebull;
	return X ^ (X >> 31);
}

int64 Corner(int64 X, int64 Y, uint64 Seed)
{
	const uint64 YHash = Mix(uint64(Y));
	const uint64 Hash = Mix(Seed ^ Mix(uint64(X)) ^ ((YHash << 31) | (YHash >> 33)));
	return int64((Hash >> 39) & 0x1ffffffull) - Q;
}

// floor(X * Frequency / Q) plus a nonnegative Q24 fraction. Frequency
// is in [1,Q]; split X first so no 128-bit multiplication is required.
bool Position(int64 X, int64 Frequency, int64& Cell, int64& Fraction)
{
	if (Frequency < 1 || Frequency > Q)
	{
		return false;
	}
	int64 High = X / Q;
	int64 Low = X % Q;
	if (Low < 0)
	{
		--High;
		Low += Q;
	}
	if (High > std::numeric_limits<int64>::max() / Frequency || High < std::numeric_limits<int64>::min() / Frequency)
	{
		return false;
	}
	const int64 Product = Low * Frequency;
	const int64 Base = High * Frequency;
	const int64 Extra = Product / Q;
	if (Base > std::numeric_limits<int64>::max() - Extra)
	{
		return false;
	}
	Cell = Base + Extra;
	Fraction = Product % Q;
	return Cell < std::numeric_limits<int64>::max();
}

int64 Fade(int64 T)
{
	const int64 T2 = Mul(T, T);
	const int64 T3 = Mul(T2, T);
	const int64 T4 = Mul(T3, T);
	const int64 T5 = Mul(T4, T);
	return FMath::Clamp<int64>(6 * T5 - 15 * T4 + 10 * T3, 0, Q);
}

int64 Lerp(int64 A, int64 B, int64 T)
{
	return A + Mul(B - A, T);
}

bool Noise(int64 X, int64 Y, int64 Frequency, uint64 Seed, int64& Out)
{
	int64 IX = 0, IY = 0, FX = 0, FY = 0;
	if (!Position(X, Frequency, IX, FX) || !Position(Y, Frequency, IY, FY))
	{
		return false;
	}
	const int64 TX = Fade(FX), TY = Fade(FY);
	Out = Lerp(Lerp(Corner(IX, IY, Seed), Corner(IX + 1, IY, Seed), TX),
		Lerp(Corner(IX, IY + 1, Seed), Corner(IX + 1, IY + 1, Seed), TX), TY);
	return true;
}

float FloatSource(const FVoxelLandGenerator& G, const FVoxelLandValueSource& S)
{
	return S.bParameter ? G.GetResolvedFloatParameters()[S.Index] : G.GetProgram().GetData().Constants[S.Index];
}

int32 IntSource(const FVoxelLandGenerator& G, const FVoxelLandValueSource& S)
{
	return S.bParameter ? G.GetResolvedIntParameters()[S.Index] : int32(G.GetProgram().GetData().Constants[S.Index]);
}

bool NoiseConfig(const FVoxelLandGenerator& G, const FVoxelLandNoiseConfig& N, int64& Frequency, int64& Gain,
	int64& Lacunarity, int32& Octaves)
{
	Octaves = IntSource(G, N.Octaves);
	return Quantize(FloatSource(G, N.Frequency), Frequency) && Frequency >= 1 && Frequency <= Q
		&& Quantize(FloatSource(G, N.Gain), Gain) && Gain >= 0 && Gain <= Q
		&& Quantize(FloatSource(G, N.Lacunarity), Lacunarity) && Lacunarity >= Q && Lacunarity <= 4 * Q && Octaves >= 1
		&& Octaves <= 16;
}

bool RunNoise(const FVoxelLandGenerator& G, const FVoxelLandInstruction& I, int64 X, int64 Y, int64& Out)
{
	const FVoxelLandNoiseConfig& N = G.GetProgram().GetData().Noise[I.Imm];
	int64 Frequency = 0, Gain = 0, Lacunarity = 0;
	int32 Octaves = 0;
	if (!NoiseConfig(G, N, Frequency, Gain, Lacunarity, Octaves))
	{
		return false;
	}
	if (I.Op == EVoxelLandOpcode::Value2)
	{
		Octaves = 1;
	}
	int64 Weight = Q, WeightSum = 0, Sum = 0;
	for (int32 Octave = 0; Octave < Octaves; ++Octave)
	{
		int64 Sample = 0;
		if (Frequency > Q || !Noise(X, Y, Frequency, Mix(G.GetResolvedSeeds()[N.SeedSlot] + uint64(Octave)), Sample))
		{
			return false;
		}
		Sum += Mul(Sample, Weight);
		WeightSum += Weight;
		if (Octave + 1 < Octaves)
		{
			Frequency = Mul(Frequency, Lacunarity);
			Weight = Mul(Weight, Gain);
		}
	}
	Out = WeightSum ? RoundRatio(Sum * Q, WeightSum) : 0;
	return true;
}
}

FVoxelLandError Validate(const FVoxelLandGenerator& Generator)
{
	const auto& D = Generator.GetProgram().GetData();
	if (D.SmoothFilter.bPresent || D.ExternalFilterIndex != INDEX_NONE || !D.ExternalOperators.IsEmpty())
	{
		return Fail(TEXT("Fixed lattice controls do not support filters or external operators"));
	}
	TArray<EKind> Kinds;
	Kinds.Init(EKind::Unknown, D.RegisterCount);
	for (int32 Index = 0; Index < D.Instructions.Num(); ++Index)
	{
		const FVoxelLandInstruction& I = D.Instructions[Index];
		EKind Kind = EKind::Value;
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
				Kind = EKind::X;
				break;
			case EVoxelLandOpcode::LoadY:
				Kind = EKind::Y;
				break;
			case EVoxelLandOpcode::LoadConstant:
			case EVoxelLandOpcode::LoadFloatParameter:
			{
				int64 Value = 0;
				const float Source = I.Op == EVoxelLandOpcode::LoadConstant
									   ? D.Constants[I.Imm]
									   : Generator.GetResolvedFloatParameters()[I.Imm];
				if (!Quantize(Source, Value))
				{
					return Fail(TEXT("Fixed lattice constant must be finite and within [-64,64]"), Index);
				}
				break;
			}
			case EVoxelLandOpcode::Value2:
			case EVoxelLandOpcode::Fbm2:
			{
				int64 F = 0, G = 0, L = 0;
				int32 O = 0;
				if (Kinds[I.A] != EKind::X || Kinds[I.B] != EKind::Y
					|| !NoiseConfig(Generator, D.Noise[I.Imm], F, G, L, O))
				{
					return Fail(TEXT("Fixed lattice noise requires direct X/Y and representable Q24 settings"), Index);
				}
				for (int32 Octave = 1; I.Op == EVoxelLandOpcode::Fbm2 && Octave < O; ++Octave)
				{
					F = Mul(F, L);
					if (F < 1 || F > Q)
					{
						return Fail(TEXT("Fixed lattice fBm octave frequency exceeds its range"), Index);
					}
				}
				break;
			}
			case EVoxelLandOpcode::Add:
			case EVoxelLandOpcode::Subtract:
			case EVoxelLandOpcode::Multiply:
			case EVoxelLandOpcode::Min:
			case EVoxelLandOpcode::Max:
				if (Kinds[I.A] != EKind::Value || Kinds[I.B] != EKind::Value)
				{
					return Fail(TEXT("Fixed lattice arithmetic cannot transform coordinates"), Index);
				}
				break;
			case EVoxelLandOpcode::Negate:
			case EVoxelLandOpcode::Abs:
				if (Kinds[I.A] != EKind::Value)
				{
					return Fail(TEXT("Fixed lattice arithmetic cannot transform coordinates"), Index);
				}
				break;
			case EVoxelLandOpcode::StoreDensity:
				if (Kinds[I.A] != EKind::Value)
				{
					return Fail(TEXT("Fixed lattice output must be a scalar value"), Index);
				}
				continue;
			default:
				return Fail(
					TEXT("Unsupported fixed lattice control opcode; use Value2/Fbm2 and bounded scalar arithmetic"),
					Index);
		}
		Kinds[I.Dst] = Kind;
	}
	return {};
}

FVoxelLandError Evaluate(const FVoxelLandGenerator& Generator, int64 OriginX, int64 OriginY, int64 SpacingCm,
	int32 CellsX, int32 CellsY, TArray<uint16>& Out, TFunctionRef<bool()> IsCancelled)
{
	if (IsInGameThread())
	{
		return Fail(TEXT("Fixed lattice evaluation requires a worker thread"), INDEX_NONE,
			EVoxelLandErrorCode::GameThreadNotAllowed);
	}
	if (CellsX < 1 || CellsX > 1024 || CellsY < 1 || CellsY > 1024 || SpacingCm < 1)
	{
		return Fail(TEXT("Invalid fixed lattice dimensions"), INDEX_NONE, EVoxelLandErrorCode::InvalidParameter);
	}
	int64 ExtentX = 0, ExtentY = 0;
	if (!VoxelLandM4Control::LatticePositionCm(CellsX, SpacingCm, ExtentX)
		|| !VoxelLandM4Control::LatticePositionCm(CellsY, SpacingCm, ExtentY)
		|| OriginX > std::numeric_limits<int64>::max() - ExtentX
		|| OriginY > std::numeric_limits<int64>::max() - ExtentY)
	{
		return Fail(TEXT("Fixed lattice coordinate overflow"), INDEX_NONE, EVoxelLandErrorCode::NonFiniteCoordinate);
	}
	if (FVoxelLandError Error = Validate(Generator))
	{
		return Error;
	}
	const auto& D = Generator.GetProgram().GetData();
	TArray<int64> Registers;
	Registers.SetNumZeroed(D.RegisterCount);
	TArray<uint16> Values;
	Values.Reserve((CellsX + 1) * (CellsY + 1));
	for (int32 Y = 0; Y <= CellsY; ++Y)
	{
		if (IsCancelled())
		{
			return Fail(TEXT("Fixed lattice evaluation cancelled"), INDEX_NONE, EVoxelLandErrorCode::Cancelled);
		}
		for (int32 X = 0; X <= CellsX; ++X)
		{
			const int64 WorldX = OriginX + int64(X) * SpacingCm, WorldY = OriginY + int64(Y) * SpacingCm;
			for (int32 Index = 0; Index < D.Instructions.Num(); ++Index)
			{
				const auto& I = D.Instructions[Index];
				int64 V = 0;
				switch (I.Op)
				{
					case EVoxelLandOpcode::LoadX:
					case EVoxelLandOpcode::LoadY:
						continue;
					case EVoxelLandOpcode::LoadConstant:
						Quantize(D.Constants[I.Imm], V);
						break;
					case EVoxelLandOpcode::LoadFloatParameter:
						Quantize(Generator.GetResolvedFloatParameters()[I.Imm], V);
						break;
					case EVoxelLandOpcode::Value2:
					case EVoxelLandOpcode::Fbm2:
						if (!RunNoise(Generator, I, WorldX, WorldY, V))
						{
							return Fail(TEXT("Fixed lattice noise coordinate or frequency overflow"), Index,
								EVoxelLandErrorCode::InvalidMathDomain);
						}
						break;
					case EVoxelLandOpcode::Add:
						V = Registers[I.A] + Registers[I.B];
						break;
					case EVoxelLandOpcode::Subtract:
						V = Registers[I.A] - Registers[I.B];
						break;
					case EVoxelLandOpcode::Multiply:
						V = Mul(Registers[I.A], Registers[I.B]);
						break;
					case EVoxelLandOpcode::Min:
						V = FMath::Min(Registers[I.A], Registers[I.B]);
						break;
					case EVoxelLandOpcode::Max:
						V = FMath::Max(Registers[I.A], Registers[I.B]);
						break;
					case EVoxelLandOpcode::Negate:
						V = -Registers[I.A];
						break;
					case EVoxelLandOpcode::Abs:
						V = FMath::Abs(Registers[I.A]);
						break;
					case EVoxelLandOpcode::StoreDensity:
					{
						const int64 Clamped = FMath::Clamp<int64>(Registers[I.A], -Q, Q);
						Values.Add(uint16(((Clamped + Q) * 65535 + Q) / (2 * Q)));
						continue;
					}
					default:
						return Fail(TEXT("Unsupported fixed lattice opcode"), Index);
				}
				if (V < -Limit || V > Limit)
				{
					return Fail(
						TEXT("Fixed lattice scalar exceeds [-64,64]"), Index, EVoxelLandErrorCode::InvalidMathDomain);
				}
				Registers[I.Dst] = V;
			}
		}
	}
	if (IsCancelled())
	{
		return Fail(TEXT("Fixed lattice evaluation cancelled"), INDEX_NONE, EVoxelLandErrorCode::Cancelled);
	}
	Out = MoveTemp(Values);
	return {};
}
}
