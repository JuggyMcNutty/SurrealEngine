
#include "Precomp.h"
#include "UConversation.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/ConSys/UConFlagRef.h"
#include "Packages/ConSys/Events/UConEvent.h"
#include "Packages/ConSys/Events/UConEventSpeech.h"
#include "Packages/ConSys/Events/UConEventTransferObject.h"
#include "Packages/ConSys/Events/UConEventCheckObject.h"
#include "Packages/ConSys/Events/UConEventAnimation.h"
#include "Packages/ConSys/Events/UConEventTrade.h"
#include "Engine.h"

// An event's name matches an actor's when both are set and spell the same,
// ignoring case; two empty names never match.
static bool MatchesName(const std::string& eventName, const NameString& actorName)
{
	NameString n(eventName);
	return !n.IsNone() && n == actorName;
}

void UConversation::BindActorEvents(UObject* actorToBind)
{
	// The original offers the one actor to each event, as the invoker too:
	// the script calls this after BindEvents when ownerRefCount is above 1,
	// so a conversation several actors share binds the one that started it.
	UActor* actor = UObject::Cast<UActor>(actorToBind);
	if (!actor)
		return;
	BindEventsToActor(actor, actor);
}

void UConversation::BindEvents(UObject** conBoundActors, UObject* invokeActor)
{
	// The original's DConversation::BindEvents: empty the ten slots, offer
	// every pawn and decoration to each event, and keep each actor an event
	// took, so the script's ActorDestroyed knows the conversation's actors.
	for (int i = 0; i < 10; i++)
		conBoundActors[i] = nullptr;

	UActor* invoker = UObject::Cast<UActor>(invokeActor);
	NameString invokerName = invoker ? NameString(invoker->BindName()) : NameString();

	int boundCount = 0;
	for (UActor* actor : engine->Level->Actors)
	{
		if (!actor)
			continue;
		if (!UObject::TryCast<UPawn>(actor) && !UObject::TryCast<UDecoration>(actor))
			continue;

		NameString name = actor->BindName();
		if (name.IsNone())
			continue;

		// Of several actors with one name, the invoker is the one bound.
		if (actor != invoker && name == invokerName)
			continue;

		if (BindEventsToActor(actor, invoker) && boundCount < 10)
			conBoundActors[boundCount++] = actor;
	}
}

bool UConversation::BindEventsToActor(UActor* actor, UActor* invokeActor)
{
	NameString name = actor->BindName();
	NameString barkName = actor->BarkBindName();
	bool isInvoker = (actor == invokeActor);
	bool taken = false;

	for (UConEvent* e = eventList(); e; e = e->nextEvent())
	{
		switch ((EEventType)e->eventType())
		{
		case EEventType::Speech:
			if (auto speech = UObject::Cast<UConEventSpeech>(e))
			{
				// The speaker and the one spoken to bind by name, or by
				// bark name when the actor is the invoker.
				if (MatchesName(speech->speakerName(), name) || (isInvoker && MatchesName(speech->speakerName(), barkName)))
				{
					speech->speaker() = actor;
					taken = true;
				}
				if (MatchesName(speech->speakingToName(), name) || (isInvoker && MatchesName(speech->speakingToName(), barkName)))
				{
					speech->speakingTo() = actor;
					taken = true;
				}
			}
			break;

		case EEventType::TransferObject:
			if (auto transfer = UObject::Cast<UConEventTransferObject>(e))
			{
				// The giver and the receiver bind by name, or by bark name
				// in a first-person conversation; a bind also loads the
				// item's class, which the package stores only as a name.
				bool bound = false;
				if (MatchesName(transfer->fromName(), name) || (bFirstPerson() && MatchesName(transfer->fromName(), barkName)))
				{
					transfer->fromActor() = actor;
					bound = true;
				}
				if (MatchesName(transfer->toName(), name) || (bFirstPerson() && MatchesName(transfer->toName(), barkName)))
				{
					transfer->toActor() = actor;
					bound = true;
				}
				if (bound)
				{
					transfer->giveObject() = engine->packages->FindClass("DeusEx." + transfer->ObjectName());
					if (!transfer->giveObject())
						LogMessage("Could not find class for TransferObject: " + transfer->ObjectName());
					taken = true;
				}
			}
			break;

		case EEventType::CheckObject:
			if (auto check = UObject::Cast<UConEventCheckObject>(e))
			{
				// Binds no actor; loads the class, or none for a nano key,
				// which the script looks for on the key ring.
				if (check->ObjectName().starts_with("NK_"))
				{
					check->checkObject() = nullptr;
				}
				else
				{
					check->checkObject() = engine->packages->FindClass("DeusEx." + check->ObjectName());
					if (!check->checkObject())
						LogMessage("Could not find class for CheckObject: " + check->ObjectName());
				}
			}
			break;

		case EEventType::Animation:
			if (auto animation = UObject::Cast<UConEventAnimation>(e))
			{
				if (MatchesName(animation->eventOwnerName(), name) || (bFirstPerson() && MatchesName(animation->eventOwnerName(), barkName)))
				{
					animation->eventOwner() = actor;
					taken = true;
				}
			}
			break;

		case EEventType::Trade:
			if (auto trade = UObject::Cast<UConEventTrade>(e))
			{
				if (MatchesName(trade->eventOwnerName(), name) || (bFirstPerson() && MatchesName(trade->eventOwnerName(), barkName)))
				{
					trade->eventOwner() = actor;
					taken = true;
				}
			}
			break;

		// Move camera binds nothing: none of the game's camera events
		// names an actor. The other events bind nothing.
		default:
			break;
		}
	}

	return taken;
}

void UConversation::ClearBindEvents()
{
	// The original's does nothing: it walks the events and calls nothing,
	// so an event keeps the last actor bound to it.
}

UObject* UConversation::CreateConCamera()
{
	UClass* cls = engine->packages->FindClass("ConSys.ConCamera");
	NameString name;
	return engine->LevelPackage->NewObject(name, cls, ObjectFlags::Transient, true);
}

UObject* UConversation::CreateFlagRef(const NameString& FlagName, bool flagValue)
{
	UClass* cls = engine->packages->FindClass("ConSys.ConFlagRef");
	NameString name;
	UObject* obj = engine->LevelPackage->NewObject(name, cls, ObjectFlags::Transient, true);
	UConFlagRef* flagObj = UObject::Cast<UConFlagRef>(obj);
	flagObj->FlagName() = FlagName;
	flagObj->Value() = flagValue;
	return obj;
}

UObject* UConversation::GetSpeechAudio(int soundID)
{
	// One sound by name, as the original: ConAudio<audioPackageName>_<id>
	// from <package>Audio<audioPackageName>.u, where <package> is the
	// conversation's own package less a final "Text". Going through the
	// package's ConAudioList instead loaded every sound in it.
	if (soundID < 0)
		return nullptr;

	std::string prefix = package->GetPackageName().ToString();
	if (prefix.size() >= 4 && NameString(prefix.substr(prefix.size() - 4)) == "Text")
		prefix.resize(prefix.size() - 4);

	auto audioPackage = engine->packages->GetPackage(prefix + "Audio" + audioPackageName());
	UObject* sound = audioPackage->GetUObject("Sound", "ConAudio" + audioPackageName() + "_" + std::to_string(soundID));
	if (!sound)
		LogMessage("Could not find sound ConAudio" + audioPackageName() + "_" + std::to_string(soundID) + " in Conversation.GetSpeechAudio");
	return sound;
}

float UConversation::GetSpeechLength(int soundID)
{
	// 0 for -1 or no sound, as the original; the script waits this long.
	USound* sound = UObject::Cast<USound>(GetSpeechAudio(soundID));
	return sound ? sound->GetDuration() : 0.0f;
}
