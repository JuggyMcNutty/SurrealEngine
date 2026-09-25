
#include "Precomp.h"
#include "UGameDirectory.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include "Package/PackageManager.h"

void UDXGameDirectory::GetGameDirectory()
{
	if (GameDirectoryType() == EGameDirectoryTypes::GD_Maps)
	{
		currentDirectory = fs::path(engine->LaunchInfo.gameRootFolder) / "Maps";
		PopulateDirectoryList();
	}
	else
	{
		currentDirectory = engine->packages->GetSaveFolderPath();
		PopulateDirectoryList();
		PopulateSaveInfoPointers();
	}
}

int UDXGameDirectory::GetNewSaveFileIndex()
{
	// The highest existing SaveNNNN plus one, never a gap (the original's).
	return engine->NextSaveSlot();
}

std::string UDXGameDirectory::GenerateSaveFilename(int saveIndex)
{
	return GetSaveIndexFolderName(saveIndex);
}

std::string UDXGameDirectory::GenerateNewSaveFileName(std::optional<int> newIndex)
{
	return GenerateSaveFilename(newIndex ? *newIndex : GetNewSaveFileIndex());
}

int UDXGameDirectory::GetDirCount()
{
	return (int)DirectoryList().size();
}

std::string UDXGameDirectory::GetDirFilename(int fileIndex)
{
	auto list = DirectoryList();
	if (fileIndex < 0 || (size_t)fileIndex >= list.size())
		return {};
	return list[fileIndex];
}

void UDXGameDirectory::SetDirType(EGameDirectoryTypes newDirType)
{
	GameDirectoryType() = newDirType;
}

void UDXGameDirectory::SetDirFilter(const std::string& strFilter)
{
	CurrentFilter() = strFilter;
}

UDXSaveInfo* UDXGameDirectory::GetSaveInfo(int fileIndex)
{
	if (GameDirectoryType() != EGameDirectoryTypes::GD_SaveGames)
		// We're not in the Save folder
		return nullptr;

	auto pkg = engine->packages->GetSaveInfoPackage(GetSaveIndexFolderName(fileIndex));
	if (!pkg)
	{
		// A save made this session is not scanned yet.
		engine->packages->ScanSaveInfos();
		pkg = engine->packages->GetSaveInfoPackage(GetSaveIndexFolderName(fileIndex));
	}
	if (!pkg)
		return nullptr;

	auto info = Cast<UDXSaveInfo>(pkg->GetUObject("DeusExSaveInfo", "MyDeusExSaveInfo"));
	if (info)
	{
		// Kept, as the original keeps it, so DeleteSaveInfo can let it go.
		auto list = LoadedSaveInfoPointers();
		bool kept = false;
		for (auto& it : list)
			kept |= (it == info);
		if (!kept)
			list.push_back(info);
	}
	return info;
}

UDXSaveInfo* UDXGameDirectory::GetSaveInfoFromDirectoryIndex(int DirectoryIndex)
{
	for (const auto& dxSaveInfo : LoadedSaveInfoPointers())
		if (dxSaveInfo->DirectoryIndex() == DirectoryIndex)
			return dxSaveInfo;

	return nullptr;
}

UDXSaveInfo* UDXGameDirectory::GetTempSaveInfo()
{
	return TempSaveInfo();
}

// Lets go of a kept save info: its file and the object. Nothing is deleted
// on disk; the screens delete a save with the DELETEGAME console command.
void UDXGameDirectory::DeleteSaveInfo(UDXSaveInfo& saveInfo)
{
	auto list = LoadedSaveInfoPointers();
	for (size_t i = 0; i < list.size(); i++)
	{
		if (list[i] == &saveInfo)
		{
			for (size_t j = i + 1; j < list.size(); j++)
				list[j - 1] = list[j];
			list.Array->Resize(list.size() - 1);
			engine->packages->RemoveSaveInfoPackage(GetSaveIndexFolderName(saveInfo.DirectoryIndex()));
			return;
		}
	}
}

// Lets go of every kept save info. Nothing is deleted on disk.
void UDXGameDirectory::PurgeAllSaveInfo()
{
	auto list = LoadedSaveInfoPointers();
	for (size_t i = 0; i < list.size(); i++)
	{
		if (list[i])
			engine->packages->RemoveSaveInfoPackage(GetSaveIndexFolderName(list[i]->DirectoryIndex()));
	}
	list.Array->Resize(0);
}

int UDXGameDirectory::GetSaveFreeSpace()
{
	// Returns a value in KBs, which limits us to ~2TB of "free space" max.
	// Should be enough but still
	const auto freeSpaceInKBs = static_cast<int>(fs::space(currentDirectory).free / 1024);
	// Capped at 1TB
	return std::min(freeSpaceInKBs, 1000 * 1024 * 1024);
}

int UDXGameDirectory::GetSaveDirectorySize(int saveIndex)
{
	if (GameDirectoryType() != EGameDirectoryTypes::GD_SaveGames)
		// We're not in the Save folder
		return 0;

	int size = 0;

	for (auto& p : fs::directory_iterator(currentDirectory / GetSaveIndexFolderName(saveIndex)))
		size += (int)p.file_size();

	return size;
}

std::string UDXGameDirectory::GetSaveIndexFolderName(int saveIndex)
{
	return engine->SaveSlotFolderName(saveIndex);
}

void UDXGameDirectory::PopulateDirectoryList()
{
	auto list = DirectoryList();
	list.Array->Resize(0);

	if (!fs::exists(currentDirectory) || !fs::is_directory(currentDirectory))
		return;

	if (GameDirectoryType() == EGameDirectoryTypes::GD_Maps)
	{
		for (auto& p : fs::directory_iterator(currentDirectory))
			if (p.is_regular_file())
				list.push_back(p.path().filename().string());
	}
	else
	{
		// The save listing is the SaveNNNN directories; the quick save and
		// Current are asked for by slot (-1, -2), never listed.
		for (auto& p : fs::directory_iterator(currentDirectory))
		{
			if (!p.is_directory())
				continue;
			const std::string name = p.path().filename().string();
			if (name.size() >= 5 && name.compare(0, 4, "Save") == 0 && name[4] >= '0' && name[4] <= '9')
				list.push_back(name);
		}
	}
}

void UDXGameDirectory::PopulateSaveInfoPointers()
{
	engine->packages->ScanSaveInfos();

	auto list = LoadedSaveInfoPointers();
	list.Array->Resize(0);

	for (auto& dir : DirectoryList())
	{
		auto pkg = engine->packages->GetSaveInfoPackage(dir);
		if (!pkg)
			continue;
		if (auto info = Cast<UDXSaveInfo>(pkg->GetUObject("DeusExSaveInfo", "MyDeusExSaveInfo")))
			list.push_back(info);
	}
}
