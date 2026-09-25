#pragma once

#include "Packages/Core/UObject.h"

class UActor;
class ULevelInfo;

// How NPCs learn of shots, footsteps, noises, alarms, bodies and distress:
// actors raise named events, NPCs listen for them, and each frame this
// manager works out who senses what and calls the listeners' script. The
// original is Engine.dll's UEventManager, a C++ class with no script; the
// game's docs/re/engine-dll.md has the read. One lives in each level's
// package (LevelInfo.EventManager), so a save keeps every listener and
// event; the original's own saved layout is not read yet, so a save of the
// original game carries a manager this Load recognizes and skips.
class UEventManager : public UObject
{
public:
	using UObject::UObject;

	void Load(ObjectStream* stream) override;
	void Save(PackageStreamWriter* stream) override;

	// The one in the level's LevelInfo.EventManager, or none: the natives
	// do nothing without one, and only InitEventManager creates it.
	static UEventManager* Get();
	static void InitEventManager(ULevelInfo* info);

	void SetEventCallback(UActor* actor, const NameString& eventName, const NameString& callback, const NameString& scoreCallback, bool bCheckVisibility, bool bCheckDir, bool bCheckCylinder, bool bCheckLOS);
	void ClearEventCallback(UActor* actor, const NameString& eventName);
	// A pulse (AISendEvent), or with start also the current level
	// (AIStartEvent): what the sender goes on giving off.
	void SendEvent(UActor* actor, const NameString& eventName, uint8_t sense, float value, float radius, bool start);
	void EndEvent(UActor* actor, const NameString& eventName, uint8_t sense);
	void ClearEvent(UActor* actor, const NameString& eventName);
	void ActorDestroyed(UActor* actor);
	void Tick();

	// EAIEventType: visual, audio or olfactory.
	enum { SenseVisual = 0, SenseAudio = 1, SenseSmell = 2 };
	// EAIEventState.
	enum { StateBegin = 0, StateEnd = 1, StatePulse = 2, StateChangeBest = 3 };

	struct SenseLevels
	{
		float Visual = 0.0f;
		float Audio = 0.0f;
		float AudioRadius = 0.0f;
		float Smell = 0.0f;
		bool Any() const { return Visual > 0.0f || Audio > 0.0f || Smell > 0.0f; }
	};

	static const int NumSlots = 16;

	struct Sender
	{
		UActor* Actor = nullptr;
		// Four numbers for each of the last 16 frames, in a ring, and the
		// current level of each.
		SenseLevels Slots[NumSlots];
		SenseLevels Current;
		bool Delete = false;
	};

	struct Receiver
	{
		UActor* Actor = nullptr;
		NameString Callback;      // called as AIEvent when none
		NameString ScoreCallback; // none: the score is distance squared plus 1
		bool bCheckVisibility = true;
		bool bCheckDir = true;
		bool bCheckCylinder = false;
		bool bCheckLOS = true;
		bool EventOn = false;
		// The receiver's XAIParams: the best sender and what was sensed.
		UActor* BestActor = nullptr;
		float BestScore = 0.0f;
		float BestVisibility = 0.0f;
		float BestVolume = 0.0f;
		float BestSmell = 0.0f;
		int LastTurnFrame = 0;
		bool Delete = false;
	};

	struct EventType
	{
		NameString Name;
		std::vector<std::unique_ptr<Sender>> Senders;
		std::vector<std::unique_ptr<Receiver>> Receivers;
	};

	std::map<NameString, EventType> Events;

	// Every receiver is in one ring the manager walks, resuming where the
	// last frame stopped; a new receiver goes at the ring's end.
	std::vector<std::pair<EventType*, Receiver*>> Ring;
	size_t RingPos = 0;
	int FrameCounter = 0;
	int SlotIndex = 0;

private:
	EventType& GetEventType(const NameString& name);
	Sender* FindSender(EventType& type, UActor* actor, bool create);
	Receiver* FindReceiver(EventType& type, UActor* actor);
	bool ProcessReceiver(EventType& type, Receiver& receiver);
	SenseLevels PeakLevels(const Sender& sender, int slotsBack) const;
	void ComputeSenseDetection(Receiver& receiver, UActor* senderActor, const SenseLevels& peak, float& visibility, float& volume, float& smell);
	void CallListener(EventType& type, Receiver& receiver, uint8_t state);
	void CleanupEvents();
};
