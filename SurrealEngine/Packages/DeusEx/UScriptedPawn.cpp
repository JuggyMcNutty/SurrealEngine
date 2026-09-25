
#include "Precomp.h"
#include "UScriptedPawn.h"

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
