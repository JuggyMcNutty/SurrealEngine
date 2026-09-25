
#include "Precomp.h"
#include "UDeusExPlayer.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/DeusEx/UGameDirectory.h"
#include "Packages/ConSys/History/UConHistory.h"
#include "Utils/Logger.h"

void UDeusExPlayer::ConBindEvents()
{
	DeusExConBindEvents();
}

UObject* UDeusExPlayer::CreateDataVaultImageNoteObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DataVaultImageNote");
	return engine->LevelPackage->NewObject("DataVaultImageNote", cls, ObjectFlags::NoFlags);
}

UObject* UDeusExPlayer::CreateDumpLocationObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DumpLocation");
	return engine->LevelPackage->NewObject("DumpLocation", cls, ObjectFlags::NoFlags);
}

UObject* UDeusExPlayer::CreateGameDirectoryObject()
{
	// A new one each call, in the transient package, both as the original's;
	// the scripts CriticalDelete it after use, which the fork's garbage
	// collector honours later.
	auto cls = engine->packages->FindClass("DeusEx.GameDirectory");
	return Cast<UDXGameDirectory>(engine->packages->GetTransientPackage()->NewObject("GameDirectory", cls, ObjectFlags::Transient));
}

UObject* UDeusExPlayer::CreateHistoryEvent()
{
	auto cls = engine->packages->FindClass("ConSys.ConHistoryEvent");
	return engine->LevelPackage->NewObject("ConHistoryEvent", cls, ObjectFlags::NoFlags);
}

UObject* UDeusExPlayer::CreateHistoryObject()
{
	auto cls = engine->packages->FindClass("ConSys.ConHistory");
	return Cast<UConHistory>(engine->LevelPackage->NewObject("ConHistory", cls, ObjectFlags::NoFlags));
}

UObject* UDeusExPlayer::CreateLogObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DeusExLog");
	return engine->LevelPackage->NewObject("DeusExLog", cls, ObjectFlags::NoFlags);
}

void UDeusExPlayer::DeleteSaveGameFiles(std::optional<std::string> saveDirectory)
{
	engine->DeleteSaveGameFiles(saveDirectory.value_or(""));
}

std::string UDeusExPlayer::GetDeusExVersion()
{
	return "1.112fm. Surreal Engine Edition!";
}

void UDeusExPlayer::SaveGame(int saveIndex, std::optional<std::string> saveDesc)
{
	engine->SaveGameInfo.SaveGameSlot = saveIndex;
	engine->SaveGameInfo.SaveGameDescription = saveDesc.value_or("");
}

NameString UDeusExPlayer::SetBoolFlagFromString(const std::string& flagNameString, bool bValue)
{
	// Not called directly from script
	LogUnimplemented("DeusExPlayer.SetBoolFlagFromString");
	return {};
}

void UDeusExPlayer::UnloadTexture(UObject* Texture)
{
	// Nothing going on here because SE never unloads textures atm. This is just here so it doesn't throw LogUnimplemented.
}
