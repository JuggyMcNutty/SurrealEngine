
#include "Precomp.h"
#include "UScriptedPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "VM/ScriptCall.h"

void UScriptedPawn::AddCarcass(const NameString& CarcassName)
{
	if (NumCarcasses() < 4)
	{
		bool carcassSeen = HaveSeenCarcass(CarcassName);
		if (carcassSeen == false)
		{
			Carcasses()[NumCarcasses()] = CarcassName;
			NumCarcasses() = NumCarcasses() + 1;
		}
	}
}

void UScriptedPawn::ConBindEvents()
{
	DeusExConBindEvents();
}

uint8_t UScriptedPawn::GetAllianceType(const NameString& AllianceName)
{
	auto alliex = AlliancesEx();
	EAllianceType result = EAllianceType::ALLIANCE_Neutral;
	for (int i = 0; i < 16; i++)
	{
		if (alliex[i].AllianceName == AllianceName)
		{
			if ((alliex[i].AllianceLevel < 0.0) || (alliex[i].AllianceAgitation >= 1.0))
			{
				result = EAllianceType::ALLIANCE_Hostile;
			}
			else if (alliex[i].AllianceLevel > 0.0)
			{
				result = EAllianceType::ALLIANCE_Friendly;
			}
			break;
		}
	}

	if (bLikesNeutral() && (result == EAllianceType::ALLIANCE_Neutral))
	{
		result = EAllianceType::ALLIANCE_Friendly;
	}
	if (bReverseAlliances())
	{
		if (result == EAllianceType::ALLIANCE_Friendly)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
		if (result == EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Friendly;
		}
	}
	return (uint8_t)result;
}

uint8_t UScriptedPawn::GetPawnAllianceType(UPawn* QueryPawn)
{
	// None is Neutral, as the original answers it.
	if (!QueryPawn)
		return (uint8_t)EAllianceType::ALLIANCE_Neutral;

	if (UScriptedPawn* qp = UObject::TryCast<UScriptedPawn>(QueryPawn))
	{
		uint8_t othersAlliance = qp->GetAllianceType(Alliance());
		if (othersAlliance == (uint8_t)EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
	}
	return GetAllianceType(QueryPawn->Alliance());
}

bool UScriptedPawn::HaveSeenCarcass(const NameString& CarcassName)
{
	for (int i = 0; i < NumCarcasses(); i++)
	{
		if (Carcasses()[i] == CarcassName)
		{
			return true;
		}
	}
	return false;
}

// A pawn this one could take on (DeusEx.dll AScriptedPawn::IsValidEnemy): any
// pawn, the player too, but itself, one being destroyed, one AI cannot
// detect or a dead one; and one of an alliance this pawn is hostile to, which
// the original's exec function checks unless told not to.
bool UScriptedPawn::IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance)
{
	if (!TestEnemy || TestEnemy == this || TestEnemy->bDeleteMe() || !TestEnemy->bDetectable() || TestEnemy->Health() <= 0)
		return false;
	if (bCheckAlliance.value_or(true) && GetPawnAllianceType(TestEnemy) != (uint8_t)EAllianceType::ALLIANCE_Hostile)
		return false;
	return true;
}

// The original's AScriptedPawn::Tick (0x100195a0), which runs before the
// actor tick: disappearing, the pivot's easing, agitation and fear -- the
// script's own unused UpdateAgitation and UpdateFear, which mirror the
// DLL's -- the sixteen AI timers, cloaking, the advanced-tactics
// manoeuvre's end, burning out and bleeding. Simulated here: in single
// player, which the fork always is, that is every tick.
void UScriptedPawn::Tick(float elapsed)
{
	if (Role() >= ROLE_SimulatedProxy)
	{
		// A bDisappear pawn in stasis or unseen for 5 s is destroyed, and
		// the tick ends there.
		if (bDisappear() && (InStasis() || Level()->TimeSeconds() - LastRenderTime() > 5.0f))
		{
			Destroy();
			return;
		}

		// The pivot moves toward DesiredPrePivot in a straight line,
		// reaching it as PrePivotTime runs out.
		if (PrePivotTime() > 0.0f)
		{
			if (elapsed >= PrePivotTime())
			{
				PrePivot() = DesiredPrePivot();
				PrePivotTime() = 0.0f;
			}
			else
			{
				PrePivot() = PrePivot() + (DesiredPrePivot() - PrePivot()) * (elapsed / PrePivotTime());
				PrePivotTime() -= elapsed;
			}
		}

		CallEvent(this, "UpdateAgitation", { ExpressionValue::FloatValue(elapsed) });
		CallEvent(this, "UpdateFear", { ExpressionValue::FloatValue(elapsed) });

		auto countDown = [&](float& timer) { timer = std::max(timer - elapsed, 0.0f); };
		countDown(AlarmTimer());
		countDown(FireTimer());
		countDown(SpecialTimer());
		countDown(AvoidWallTimer());
		countDown(AvoidBumpTimer());
		countDown(ObstacleTimer());
		countDown(CloakEMPTimer());
		countDown(TakeHitTimer());
		countDown(CarcassCheckTimer());
		countDown(BeamCheckTimer());
		countDown(FutzTimer());
		countDown(PlayerAgitationTimer());

		if (Weapon())
			countDown(ReloadTimer());
		else
			ReloadTimer() = 0.0f;

		if (PotentialEnemyTimer() > 0.0f)
		{
			countDown(PotentialEnemyTimer());
			if (PotentialEnemyTimer() == 0.0f)
				PotentialEnemyAlliance() = NameString();
		}

		if (Weapon())
			WeaponTimer() += elapsed;
		else
			WeaponTimer() = 0.0f;

		if (DistressTimer() >= 0.0f)
		{
			DistressTimer() += elapsed;
			if (DistressTimer() > FearSustainTime())
				DistressTimer() = -1.0f;
		}

		if (bHasCloak())
			CallEvent(this, "EnableCloak", { ExpressionValue::BoolValue(Health() <= CloakThreshold()) });

		// The manoeuvre ends once the pawn stops accelerating, leaves
		// walking or has no turn direction.
		if (bAdvancedTactics())
		{
			bool accelerating = dot(Acceleration(), Acceleration()) > 0.0f;
			bool turning = TurnDirection() != 0; // TURNING_None
			if (!accelerating || Physics() != PHYS_Walking || !turning)
			{
				bAdvancedTactics() = false;
				if (turning)
					MoveTimer() -= 4.0f;
				ActorAvoiding() = nullptr;
				NextDirection() = 0;
				TurnDirection() = 0;
				bClearedObstacle() = true;
				ObstacleTimer() = 0.0f;
			}
		}

		// A burning pawn goes out past its BurnPeriod.
		if (bOnFire())
		{
			burnTimer() += elapsed;
			if (burnTimer() > BurnPeriod())
				CallEvent(this, "ExtinguishFire");
		}

		// A wounded pawn drips faster the harder it bleeds and the faster
		// it moves; the wound clots over ClotPeriod. With bTickVisibleOnly,
		// only within 1,200 units of the player.
		if (bCanBleed() && BleedRate() > 0.0f && (!bTickVisibleOnly() || DistanceFromPlayer() <= 1200.0f))
		{
			float speedShare = std::clamp(length(Velocity()) / 512.0f, 0.05f, 1.0f);
			float period = (1.1f - BleedRate()) / speedShare;
			DropCounter() += elapsed;
			while (period > 0.0f && DropCounter() >= period)
			{
				DropCounter() -= period;
				CallEvent(this, "SpurtBlood");
			}
			BleedRate() -= ClotPeriod() > 0.0f ? elapsed / ClotPeriod() : BleedRate();
			if (BleedRate() <= 0.0f)
			{
				BleedRate() = 0.0f;
				DropCounter() = 0.0f;
			}
		}
	}

	UPawn::Tick(elapsed);
}
