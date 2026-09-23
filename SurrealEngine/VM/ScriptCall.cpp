
#include "Precomp.h"
#include "ScriptCall.h"
#include "Frame.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include <unordered_map>

NameString ToNameString(EventName name)
{
	static const NameString names[] =
	{
		// Probe names:
		"Spawned",      "Destroyed",       "GainedChild",     "LostChild",
		"Probe4",       "Probe5",          "Trigger",         "UnTrigger",
		"Timer",        "HitWall",         "Falling",         "Landed",
		"ZoneChange",   "Touch",           "UnTouch",         "Bump",
		"BeginState",   "EndState",        "BaseChange",      "Attach",
		"Detach",       "ActorEntered",    "ActorLeaving",    "KillCredit",
		"AnimEnd",      "EndedRotation",   "InterpolateEnd",  "EncroachingOn",
		"EncroachedBy", "FootZoneChange",  "HeadZoneChange",  "PainTimer",
		"SpeechTimer",  "MayFall",         "Probe34",         "Die",
		"Tick",         "PlayerTick",      "Expired",         "Probe39",
		"SeePlayer",    "EnemyNotVisible", "HearNoise",       "UpdateEyeHeight",
		"SeeMonster",   "SeeFriend",       "SpecialHandling", "BotDesireability",
		"Probe48",      "Probe49",         "Probe50",         "Probe51",
		"Probe52",      "Probe53",         "Probe54",         "Probe55",
		"Probe56",      "Probe57",         "Probe58",         "Probe59",
		"Probe60",      "Probe61",         "Probe62",         "All",

		// Other events:
		"PlayerCalcView", "Resolved", "ResolveFailed", "PreBeginPlay",
		"BeginPlay", "PostBeginPlay", "SetInitialState", "SpawnNotification",
		"PostTouch", "FellOutOfWorld", "UpdateTactics", "PlayerInput",
		"Reset", "PreRender", "RenderOverlays", "PostRender",
		"NotifyLevelChange", "InitGame", "PreLogin", "Login",
		"Possess", "TravelPreAccept", "AcceptInventory", "TravelPostAccept",
		"PostLogin", "KeyType", "KeyEvent"
	};
	return names[(int)name];
}

// This is so nasty. Maybe we should just put the probe names at the top of the name index table?
static std::unordered_map<int, EventName> CreateLookup()
{
	std::unordered_map<int, EventName> lookup;
	for (int i = 0; i < (int)EventName::MaxEventNameValue; i++)
	{
		EventName name = (EventName)i;
		lookup[ToNameString(name).GetCompareIndex()] = name;
	}
	return lookup;
}

bool NameStringToEventName(const NameString& name, EventName& eventName)
{
	static std::unordered_map<int, EventName> lookup = CreateLookup();
	auto it = lookup.find(name.GetCompareIndex());
	if (it == lookup.end())
		return false;
	eventName = it->second;
	return true;
}

ExpressionValue CallEvent(UObject* Context, EventName eventname, Array<ExpressionValue> args)
{
	if (!Context->IsEventEnabled(eventname))
		return ExpressionValue::NothingValue();

	if (auto actor = UObject::TryCast<UActor>(Context))
	{
		if (!actor->Level()->bBegunPlay() || (actor->bDeleteMe() && eventname != EventName::Destroyed))
			return ExpressionValue::NothingValue();
	}

	UFunction* func = FindEventFunction(Context, ToNameString(eventname));
	if (func)
		return Frame::Call(func, Context, std::move(args));
	else
		return ExpressionValue::NothingValue();
}

ExpressionValue CallEvent(UObject* Context, const NameString& name, Array<ExpressionValue> args)
{
	if (!Context->IsEventEnabled(name))
		return ExpressionValue::NothingValue();

	if (auto actor = UObject::TryCast<UActor>(Context))
	{
		if (!actor->Level()->bBegunPlay() || actor->bDeleteMe())
			return ExpressionValue::NothingValue();
	}

	UFunction* func = FindEventFunction(Context, name);
	if (func)
		return Frame::Call(func, Context, std::move(args));
	else
		return ExpressionValue::NothingValue();
}

UFunction* FindEventFunction(UObject* Context, const NameString& name)
{
	return FindScriptFunction(Context->Class, Context->GetStateName(), name);
}

static UFunction* SearchScriptFunction(UClass* contextClass, const NameString& stateName, const NameString& name)
{
	// Search states first

	for (UClass* cls = contextClass; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
	{
		UState* state = cls->GetState(stateName);
		if (state)
		{
			UFunction* func = state->GetFunction(name);
			if (func)
				return func;
		}
	}

	// Search normal member functions next

	for (UClass* cls = contextClass; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
	{
		UFunction* func = cls->GetFunction(name);
		if (func)
			return func;
	}

	return nullptr;
}

UFunction* FindScriptFunction(UClass* cls, const NameString& stateName, const NameString& name)
{
	// The search walks the class hierarchy's maps, and the answer depends only
	// on the class, the state and the name, none of which change once loaded.
	// Misses are kept too: an event a class does not have is asked for often.
	uint64_t key = (((uint64_t)(uint32_t)stateName.GetCompareIndex()) << 32) | (uint32_t)name.GetCompareIndex();
	auto it = cls->VirtualFunctionCache.find(key);
	if (it != cls->VirtualFunctionCache.end())
		return it->second;
	UFunction* func = SearchScriptFunction(cls, stateName, name);
	cls->VirtualFunctionCache[key] = func;
	return func;
}
