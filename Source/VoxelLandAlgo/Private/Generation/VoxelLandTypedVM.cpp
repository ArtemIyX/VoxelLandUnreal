#include "Data/Structs/VoxelLandWorkerContextImpl.h"
#include "Generation/VoxelLandPower.h"

class FVoxelLandTypedVMInternals final
{
public:
	static constexpr int32 TypedBatchSize = 256;

	static FVoxelLandError TypedError(const TCHAR* Message)
	{
		FVoxelLandError Error;
		Error.Code = EVoxelLandErrorCode::InvalidMathDomain;
		Error.Message = Message;
		return Error;
	}
};

FVoxelLandError FVoxelLandWorkerContextImpl::ExecuteTyped(
    const FVoxelLandInstruction& I, const TArray<int32>& IntParameters, int32 N)
{
	const int32 Dst = int32(I.Dst) * FVoxelLandTypedVMInternals::TypedBatchSize;
	for (int32 Sample = 0; Sample < N; ++Sample)
    {
		const auto Float = [this, Sample](uint16 Reg) {
			return Registers[int32(Reg) * FVoxelLandTypedVMInternals::TypedBatchSize + Sample];
		};
		const auto Int = [this, Sample](uint16 Reg) {
			return IntRegisters[int32(Reg) * FVoxelLandTypedVMInternals::TypedBatchSize + Sample];
		};
		float Value = 0.f;
        int32 Integer = 0;
        bool bFloat = false;
        switch (I.Op)
        {
            case EVoxelLandOpcode::LoadIntConstant:
                Integer = I.Imm <= uint32(MAX_int32) ? int32(I.Imm) : int32(int64(I.Imm) - 4294967296ll);
                break;
            case EVoxelLandOpcode::LoadBoolConstant:
                Integer = int32(I.Imm);
                break;
            case EVoxelLandOpcode::LoadIntParameter:
            case EVoxelLandOpcode::LoadBoolParameter:
                Integer = IntParameters[I.Imm];
                break;
            case EVoxelLandOpcode::IntToFloat:
                Value = float(Int(I.A));
                if (double(Value) != double(Int(I.A)))
                {
					return FVoxelLandTypedVMInternals::TypedError(
						TEXT("Integer cannot be represented exactly as float"));
				}
                bFloat = true;
                break;
            case EVoxelLandOpcode::Less: Integer = Float(I.A) < Float(I.B); break;
            case EVoxelLandOpcode::LessEqual: Integer = Float(I.A) <= Float(I.B); break;
            case EVoxelLandOpcode::Equal: Integer = Float(I.A) == Float(I.B); break;
            case EVoxelLandOpcode::BoolAnd: Integer = Int(I.A) && Int(I.B); break;
            case EVoxelLandOpcode::BoolOr: Integer = Int(I.A) || Int(I.B); break;
            case EVoxelLandOpcode::BoolNot: Integer = !Int(I.A); break;
            case EVoxelLandOpcode::SelectFloat:
                Value = Int(I.A) ? Float(I.B) : Float(I.C);
                bFloat = true;
                break;
            case EVoxelLandOpcode::SelectInt:
            case EVoxelLandOpcode::SelectBool:
                Integer = Int(I.A) ? Int(I.B) : Int(I.C);
                break;
            case EVoxelLandOpcode::Power:
                if (!FVoxelLandPower::Evaluate(Float(I.A), Float(I.B), Value))
                {
					return FVoxelLandTypedVMInternals::TypedError(TEXT("Invalid power domain or non-finite result"));
				}
                bFloat = true;
                break;
            case EVoxelLandOpcode::DynamicPlane:
            {
                const float NX = Float(I.D), NY = Float(I.E), NZ = Float(I.F);
                const double Norm = double(NX) * NX + double(NY) * NY + double(NZ) * NZ;
                if (FMath::Abs(Norm - 1.0) > 0.00001)
                {
					return FVoxelLandTypedVMInternals::TypedError(TEXT("Plane normal must be unit length"));
				}
                Value = ((Float(I.A) * NX + Float(I.B) * NY) + Float(I.C) * NZ) + Float(uint16(I.Imm));
                bFloat = true;
                break;
            }
            case EVoxelLandOpcode::DynamicSphere:
            {
                const float Radius = Float(I.D);
                if (Radius <= 0.f)
                {
					return FVoxelLandTypedVMInternals::TypedError(TEXT("Sphere radius must be positive"));
				}
                const float PosX = Float(I.A), PosY = Float(I.B), PosZ = Float(I.C);
                Value = FMath::Sqrt((PosX * PosX + PosY * PosY) + PosZ * PosZ) - Radius;
                bFloat = true;
                break;
            }
            case EVoxelLandOpcode::DynamicBox:
            {
                const float EX = Float(I.D), EY = Float(I.E), EZ = Float(I.F);
                if (EX <= 0.f || EY <= 0.f || EZ <= 0.f)
                {
					return FVoxelLandTypedVMInternals::TypedError(TEXT("Box extents must be positive"));
				}
                const float DistX = FMath::Abs(Float(I.A)) - EX, DistY = FMath::Abs(Float(I.B)) - EY,
                    DistZ = FMath::Abs(Float(I.C)) - EZ;
                const float PX = FMath::Max(DistX, 0.f), PY = FMath::Max(DistY, 0.f), PZ = FMath::Max(DistZ, 0.f);
                Value = FMath::Sqrt((PX * PX + PY * PY) + PZ * PZ) + FMath::Min(FMath::Max(DistX, FMath::Max(DistY, DistZ)), 0.f);
                bFloat = true;
                break;
            }
            case EVoxelLandOpcode::Clamp:
                if (Float(I.B) > Float(I.C))
                {
					return FVoxelLandTypedVMInternals::TypedError(TEXT("Clamp minimum exceeds maximum"));
				}
                Value = FMath::Clamp(Float(I.A), Float(I.B), Float(I.C));
                bFloat = true;
                break;
            default:
				return FVoxelLandTypedVMInternals::TypedError(TEXT("Unknown typed instruction"));
		}
        if (bFloat)
        {
            if (!FMath::IsFinite(Value))
            {
				return FVoxelLandTypedVMInternals::TypedError(TEXT("Non-finite typed result"));
			}
            Registers[Dst + Sample] = Value;
        }
        else
        {
            IntRegisters[Dst + Sample] = Integer;
        }
    }
    return {};
}
