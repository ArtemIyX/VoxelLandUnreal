#include "World/Collision/VoxelLandWalkMotor.h"

#include "Generation/Collision/VoxelLandSdfCollision.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Collision/VoxelLandWalkSettings.h"
#include "World/Collision/VoxelLandWalkState.h"

TRACE_DECLARE_INT_COUNTER(VoxelLandWalkSweepStatus, TEXT("VoxelLand/WalkSweepStatus"));

class FVoxelLandWalkStep
{
public:
	FVoxelLandWalkStep(const FVoxelLandCollisionField& InField, const FVoxelLandWalkSettings& InSettings)
		: Field(InField)
		, Settings(InSettings)
	{
	}

	bool MoveHorizontal(const FVector3d& Delta, FVoxelLandWalkState& State) const;
	bool MoveVertical(FVoxelLandWalkState& State) const;

private:
	FVoxelLandCapsuleSweepResult Sweep(const FVector3d& Start, const FVector3d& Delta) const;
	bool TryStepUp(const FVector3d& Start, const FVector3d& Delta, FVoxelLandWalkState& State) const;
	FVector3d SafeGroundCenter(
		const FVector3d& Start, const FVector3d& Drop, const FVoxelLandCapsuleSweepResult& Hit) const;

	const FVoxelLandCollisionField& Field;
	const FVoxelLandWalkSettings& Settings;
};

FVoxelLandCapsuleSweepResult FVoxelLandWalkStep::Sweep(const FVector3d& Start, const FVector3d& Delta) const
{
	FVoxelLandCapsuleSweepRequest Request;
	Request.Start = Start;
	Request.End = Start + Delta;
	Request.RadiusCm = Settings.RadiusCm;
	Request.HalfHeightCm = Settings.HalfHeightCm;
	Request.SkinCm = Settings.SkinCm;
	const FVoxelLandCapsuleSweepResult Result = FVoxelLandSdfCollision::SweepCapsule(Field, Request);
	TRACE_COUNTER_SET(VoxelLandWalkSweepStatus, static_cast<int32>(Result.Status));
	return Result;
}

FVector3d FVoxelLandWalkStep::SafeGroundCenter(
	const FVector3d& Start, const FVector3d& Drop, const FVoxelLandCapsuleSweepResult& Hit) const
{
	const double Backoff = Settings.GroundClearanceCm / Drop.Size();
	return Start + Drop * FMath::Max(0.0, Hit.LastSafeTime - Backoff);
}

bool FVoxelLandWalkStep::TryStepUp(const FVector3d& Start, const FVector3d& Delta, FVoxelLandWalkState& State) const
{
	const FVector3d Rise(0.0, 0.0, Settings.StepHeightCm);
	if (Sweep(Start, Rise).Status != EVoxelLandCapsuleSweepStatus::Clear)
	{
		return false;
	}
	const FVector3d Raised = Start + Rise;
	const FVector3d Across(Delta.X, Delta.Y, 0.0);
	if (Sweep(Raised, Across).Status != EVoxelLandCapsuleSweepStatus::Clear)
	{
		return false;
	}
	const FVector3d Top = Raised + Across;
	const FVector3d Drop(0.0, 0.0, -Settings.StepHeightCm - Settings.FloorProbeCm);
	const FVoxelLandCapsuleSweepResult Ground = Sweep(Top, Drop);
	if (Ground.Status != EVoxelLandCapsuleSweepStatus::Contact || Ground.Normal.Z < Settings.MinFloorNormalZ)
	{
		return false;
	}
	State.Center = SafeGroundCenter(Top, Drop, Ground);
	State.GroundNormal = Ground.Normal;
	return true;
}

bool FVoxelLandWalkStep::MoveHorizontal(const FVector3d& Delta, FVoxelLandWalkState& State) const
{
	if (Delta.IsNearlyZero())
	{
		return true;
	}
	const FVector3d Start = State.Center;
	const FVoxelLandCapsuleSweepResult Hit = Sweep(Start, Delta);
	if (Hit.Status == EVoxelLandCapsuleSweepStatus::Clear)
	{
		State.Center += Delta;
		return true;
	}
	if (Hit.Status != EVoxelLandCapsuleSweepStatus::Contact)
	{
		return false;
	}
	if (State.bIsGrounded && Hit.Normal.Z < Settings.MinFloorNormalZ && TryStepUp(Start, Delta, State))
	{
		return true;
	}
	State.Center += Delta * Hit.LastSafeTime;
	const FVector3d Remainder = Delta * (1.0 - Hit.LastSafeTime);
	const FVector3d Slide = Remainder - Hit.Normal * FMath::Min(0.0, FVector3d::DotProduct(Remainder, Hit.Normal));
	if (Slide.IsNearlyZero())
	{
		return true;
	}
	const FVoxelLandCapsuleSweepResult Along = Sweep(State.Center, Slide);
	if (Along.Status == EVoxelLandCapsuleSweepStatus::Clear)
	{
		State.Center += Slide;
		return true;
	}
	if (Along.Status != EVoxelLandCapsuleSweepStatus::Contact)
	{
		return false;
	}
	State.Center += Slide * Along.LastSafeTime;
	return true;
}

bool FVoxelLandWalkStep::MoveVertical(FVoxelLandWalkState& State) const
{
	State.VerticalSpeed =
		FMath::Max(State.VerticalSpeed - Settings.GravityCmPerSecondSquared * FVoxelLandWalkSettings::FixedStepSeconds,
			-Settings.TerminalSpeedCmPerSecond);
	const double FallDistance = -State.VerticalSpeed * FVoxelLandWalkSettings::FixedStepSeconds;
	const double ProbeLength = FallDistance + Settings.FloorProbeCm;
	const FVector3d Drop(0.0, 0.0, -ProbeLength);
	const FVoxelLandCapsuleSweepResult Ground = Sweep(State.Center, Drop);
	if (Ground.Status == EVoxelLandCapsuleSweepStatus::Clear)
	{
		State.Center.Z -= FallDistance;
		State.bIsGrounded = false;
		State.GroundNormal = FVector3d::UpVector;
		return true;
	}
	if (Ground.Status != EVoxelLandCapsuleSweepStatus::Contact)
	{
		return false;
	}
	State.bIsGrounded = Ground.Normal.Z >= Settings.MinFloorNormalZ;
	if (State.bIsGrounded)
	{
		State.Center = SafeGroundCenter(State.Center, Drop, Ground);
		State.GroundNormal = Ground.Normal;
		State.VerticalSpeed = 0.0;
		return true;
	}
	State.Center.Z -= FMath::Min(FallDistance, Ground.LastSafeTime * ProbeLength);
	State.GroundNormal = FVector3d::UpVector;
	State.VerticalSpeed = 0.0;
	return true;
}

bool FVoxelLandWalkMotor::Step(const FVoxelLandCollisionField& Field, const FVoxelLandWalkSettings& Settings,
	const FVector2d& HorizontalInput, FVoxelLandWalkState& State)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_WalkStep);
	const double InputLength = HorizontalInput.Size();
	if (!FMath::IsFinite(InputLength) || State.Center.ContainsNaN() || State.GroundNormal.ContainsNaN()
		|| !FMath::IsFinite(State.VerticalSpeed) || State.VerticalSpeed > 0.0
		|| !FMath::IsFinite(Settings.SpeedCmPerSecond) || Settings.SpeedCmPerSecond < 0.0
		|| !FMath::IsFinite(Settings.GravityCmPerSecondSquared) || Settings.GravityCmPerSecondSquared < 0.0
		|| !FMath::IsFinite(Settings.TerminalSpeedCmPerSecond) || Settings.TerminalSpeedCmPerSecond <= 0.0
		|| !FMath::IsFinite(Settings.FloorProbeCm) || Settings.FloorProbeCm <= 0.0
		|| !FMath::IsFinite(Settings.GroundClearanceCm) || Settings.GroundClearanceCm < 0.0
		|| !FMath::IsFinite(Settings.StepHeightCm) || Settings.StepHeightCm < 0.0
		|| !FMath::IsFinite(Settings.MinFloorNormalZ) || Settings.MinFloorNormalZ <= 0.0
		|| Settings.MinFloorNormalZ > 1.0)
	{
		return false;
	}
	const FVector2d Input = HorizontalInput / FMath::Max(1.0, InputLength);
	const double Distance = Settings.SpeedCmPerSecond * FVoxelLandWalkSettings::FixedStepSeconds;
	FVector3d Move(Input.X * Distance, Input.Y * Distance, 0.0);
	if (State.bIsGrounded && State.GroundNormal.Z >= Settings.MinFloorNormalZ)
	{
		Move.Z = -FVector3d::DotProduct(Move, State.GroundNormal) / State.GroundNormal.Z;
		Move = Move.GetClampedToMaxSize(Distance);
	}

	FVoxelLandWalkState Next = State;
	const FVoxelLandWalkStep Queries(Field, Settings);
	if (!Queries.MoveHorizontal(Move, Next) || !Queries.MoveVertical(Next))
	{
		return false;
	}
	State = Next;
	return true;
}
