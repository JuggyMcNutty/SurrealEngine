#pragma once

#include "Packages/Engine/Actors/Pawn/UPawn.h"

struct UDXInitialAllianceInfo
{
	NameString AllianceName;
	float AllianceLevel;
	union
	{
		uint32_t bPermanent : 1;
		uint32_t flags;
	};
};

struct UDXInitialAllianceInfoEx
{
	NameString AllianceName;
	float AllianceLevel;
	float AllianceAgitation;
	union
	{
		uint32_t bPermanent : 1;
		uint32_t flags;
	};
};

class UScriptedPawn : public UPawn
{
public:
	using UPawn::UPawn;
	FixedArrayView<NameString, 4> Carcasses() { return FixedArray<NameString, 4>(PropOffsets_ScriptedPawn.Carcasses); }
	FixedArrayView<UDXInitialAllianceInfo, 8> InitialAlliances() { return FixedArray<UDXInitialAllianceInfo, 8>(PropOffsets_ScriptedPawn.InitialAlliances); }
	FixedArrayView<UDXInitialAllianceInfoEx, 16> AlliancesEx() { return FixedArray<UDXInitialAllianceInfoEx, 16>(PropOffsets_ScriptedPawn.AlliancesEx); }
	int& NumCarcasses() { return Value<int>(PropOffsets_ScriptedPawn.NumCarcasses); }
	BitfieldBool bLikesNeutral() { return BoolValue(PropOffsets_ScriptedPawn.bLikesNeutral); }
	BitfieldBool bReverseAlliances() { return BoolValue(PropOffsets_ScriptedPawn.bReverseAlliances); }
	void AddCarcass(const NameString& CarcassName);
	void ConBindEvents();
	uint8_t GetAllianceType(const NameString& AllianceName);
	uint8_t GetPawnAllianceType(UPawn* QueryPawn);
	bool HaveSeenCarcass(const NameString& CarcassName);
	bool IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance);

	// The original's AScriptedPawn::Tick (docs/re/deusex-dll.md, the native
	// tick), which ends by calling the actor tick.
	void Tick(float elapsed) override;

	BitfieldBool bDisappear() { return BoolValue(PropOffsets_ScriptedPawn.bDisappear); }
	BitfieldBool bTickVisibleOnly() { return BoolValue(PropOffsets_ScriptedPawn.bTickVisibleOnly); }
	BitfieldBool bHasCloak() { return BoolValue(PropOffsets_ScriptedPawn.bHasCloak); }
	BitfieldBool bClearedObstacle() { return BoolValue(PropOffsets_ScriptedPawn.bClearedObstacle); }
	BitfieldBool bCanBleed() { return BoolValue(PropOffsets_ScriptedPawn.bCanBleed); }
	int& CloakThreshold() { return Value<int>(PropOffsets_ScriptedPawn.CloakThreshold); }
	float& PrePivotTime() { return Value<float>(PropOffsets_ScriptedPawn.PrePivotTime); }
	vec3& DesiredPrePivot() { return Value<vec3>(PropOffsets_ScriptedPawn.DesiredPrePivot); }
	float& AlarmTimer() { return Value<float>(PropOffsets_ScriptedPawn.AlarmTimer); }
	float& FireTimer() { return Value<float>(PropOffsets_ScriptedPawn.FireTimer); }
	float& SpecialTimer() { return Value<float>(PropOffsets_ScriptedPawn.SpecialTimer); }
	float& ReloadTimer() { return Value<float>(PropOffsets_ScriptedPawn.ReloadTimer); }
	float& AvoidWallTimer() { return Value<float>(PropOffsets_ScriptedPawn.AvoidWallTimer); }
	float& AvoidBumpTimer() { return Value<float>(PropOffsets_ScriptedPawn.AvoidBumpTimer); }
	float& ObstacleTimer() { return Value<float>(PropOffsets_ScriptedPawn.ObstacleTimer); }
	float& CloakEMPTimer() { return Value<float>(PropOffsets_ScriptedPawn.CloakEMPTimer); }
	float& TakeHitTimer() { return Value<float>(PropOffsets_ScriptedPawn.TakeHitTimer); }
	float& CarcassCheckTimer() { return Value<float>(PropOffsets_ScriptedPawn.CarcassCheckTimer); }
	float& PotentialEnemyTimer() { return Value<float>(PropOffsets_ScriptedPawn.PotentialEnemyTimer); }
	float& BeamCheckTimer() { return Value<float>(PropOffsets_ScriptedPawn.BeamCheckTimer); }
	float& FutzTimer() { return Value<float>(PropOffsets_ScriptedPawn.FutzTimer); }
	float& PlayerAgitationTimer() { return Value<float>(PropOffsets_ScriptedPawn.PlayerAgitationTimer); }
	float& WeaponTimer() { return Value<float>(PropOffsets_ScriptedPawn.WeaponTimer); }
	float& DistressTimer() { return Value<float>(PropOffsets_ScriptedPawn.DistressTimer); }
	float& FearSustainTime() { return Value<float>(PropOffsets_ScriptedPawn.FearSustainTime); }
	NameString& PotentialEnemyAlliance() { return Value<NameString>(PropOffsets_ScriptedPawn.PotentialEnemyAlliance); }
	UActor*& ActorAvoiding() { return Value<UActor*>(PropOffsets_ScriptedPawn.ActorAvoiding); }
	uint8_t& NextDirection() { return Value<uint8_t>(PropOffsets_ScriptedPawn.NextDirection); }
	uint8_t& TurnDirection() { return Value<uint8_t>(PropOffsets_ScriptedPawn.TurnDirection); }
	float& BurnPeriod() { return Value<float>(PropOffsets_ScriptedPawn.BurnPeriod); }
	float& BleedRate() { return Value<float>(PropOffsets_ScriptedPawn.BleedRate); }
	float& ClotPeriod() { return Value<float>(PropOffsets_ScriptedPawn.ClotPeriod); }
	float& DropCounter() { return Value<float>(PropOffsets_ScriptedPawn.DropCounter); }
};

struct XAIParams
{
	UActor* BestActor;
	float Score;
	float Visibility;
	float Volume;
	float Smell;
};
