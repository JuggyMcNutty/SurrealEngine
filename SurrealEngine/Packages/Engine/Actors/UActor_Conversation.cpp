
#include "Precomp.h"
#include "UActor.h"
#include "Package/PackageManager.h"
#include "Packages/ConSys/UConversation.h"
#include "Packages/ConSys/UConversationList.h"
#include "Packages/ConSys/UConListItem.h"
#include "Packages/ConSys/UConItem.h"
#include "Packages/DeusEx/UDeusExLevelInfo.h"
#include "Engine.h"

void UActor::DeusExConBindEvents()
{
	// The original's ConBindEvents (DeusEx.dll) and ConSys's
	// DConversationList::BindConversations: the list is the one the level's
	// ConversationPackage names, a bark binds by the actor's bark name, any
	// other conversation by its name only, and each item counts into its
	// conversation's ownerRefCount.
	UDeusExLevelInfo* info = engine->DeusExLevelInfo;
	if (!info)
	{
		LogMessage("Conversations cannot be bound: no DeusExLevelInfo");
		return;
	}
	if (info->MissionNumber() < 0)
		return;

	std::string pkgPrefix = info->ConversationPackage();
	if (pkgPrefix.empty() || NameString(pkgPrefix) == "DeusExConversations")
		pkgPrefix = "DeusExCon";

	char listName[32];
	snprintf(listName, sizeof(listName), "ConList_Mission%02d", info->MissionNumber());
	auto mission = UObject::Cast<UConversationList>(engine->packages->GetPackage(pkgPrefix + "Text")->GetUObject("ConversationList", listName));
	if (!mission)
		return;

	// PurgeBoundConversations: the old list's conversations lose this owner.
	for (UConListItem* item = UObject::Cast<UConListItem>(ConListItems()); item; item = item->Next())
	{
		if (item->con())
			item->con()->ownerRefCount()--;
	}
	ConListItems() = nullptr;

	UClass* clsConListItem = engine->packages->FindClass("ConSys.ConListItem");
	NameString bindName = BindName();
	NameString barkBindName = BarkBindName();

	for (UConItem* item = mission->conversations(); item; item = item->Next())
	{
		auto conversation = UObject::Cast<UConversation>(item->ConObject());
		if (!conversation)
			continue;

		// A bark ("_Bark" in its name, case sensitive) is owned by the
		// actor's bark name, or its name when it has none; any other
		// conversation by its name, never its bark name.
		bool isBark = conversation->conName().ToString().find("_Bark") != std::string::npos;
		NameString wanted = (isBark && !barkBindName.IsNone()) ? barkBindName : bindName;
		if (wanted.IsNone() || NameString(conversation->conOwnerName()) != wanted)
			continue;

		// Each goes to the front, so the list runs from the mission list's
		// last conversation to its first.
		NameString name;
		UConListItem* newItem = UObject::Cast<UConListItem>(engine->LevelPackage->NewObject(name, clsConListItem, ObjectFlags::Transient, true));
		newItem->con() = conversation;
		newItem->Next() = UObject::Cast<UConListItem>(ConListItems());
		ConListItems() = newItem;
		conversation->ownerRefCount()++;
	}
}
