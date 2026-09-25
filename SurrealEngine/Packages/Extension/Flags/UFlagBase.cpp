
#include "Precomp.h"
#include "UFlagBase.h"
#include "UFlag.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Extension/Flags/UFlagBool.h"
#include "Packages/Extension/Flags/UFlagByte.h"
#include "Packages/Extension/Flags/UFlagFloat.h"
#include "Packages/Extension/Flags/UFlagInt.h"
#include "Packages/Extension/Flags/UFlagName.h"
#include "Packages/Extension/Flags/UFlagRotator.h"
#include "Packages/Extension/Flags/UFlagVector.h"

// A CRC of the flag's name in upper case, as the original hashes it: the
// buckets are the CRC modulo 64, and each chain is kept in order of hash,
// then type, so there is no limit to the flags. The original's exact CRC
// polynomial was not read; it only matters for reading the original game's
// own saved flag chains, to be pinned when those saves load at all.
static uint32_t FlagNameCrc(const NameString& name)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		made = true;
	}
	uint32_t crc = 0xFFFFFFFFu;
	for (char ch : name.ToString())
	{
		uint8_t c = (uint8_t)std::toupper((unsigned char)ch);
		crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
	}
	return ~crc;
}

UFlag* UFlagBase::GetFlag(const NameString& flagName, uint8_t flagType)
{
	const int bucket = (int)(FlagNameCrc(flagName) % HashTableSize);
	for (UFlag* flag = hashTable()[bucket]; flag; flag = flag->nextFlag())
	{
		if (flag->FlagName() == flagName && flag->flagType() == flagType)
			return flag;
	}
	return nullptr;
}

bool UFlagBase::DeleteFlag(const NameString& FlagName, uint8_t flagType)
{
	const int bucket = (int)(FlagNameCrc(FlagName) % HashTableSize);
	auto table = hashTable();
	UFlag* prev = nullptr;
	for (UFlag* flag = table[bucket]; flag; prev = flag, flag = flag->nextFlag())
	{
		if (flag->FlagName() == FlagName && flag->flagType() == flagType)
		{
			if (prev)
				prev->nextFlag() = flag->nextFlag();
			else
				table[bucket] = flag->nextFlag();
			return true;
		}
	}
	return false;
}

void UFlagBase::DeleteAllFlags()
{
	auto table = hashTable();
	for (int i = 0; i < HashTableSize; i++)
		table[i] = nullptr;
}

// Deletes every flag whose expiration is not 0 and at most the criteria;
// an expiration of 0 never expires. The mission scripts call this with the
// mission's number on a level reached by travel.
void UFlagBase::DeleteExpiredFlags(int criteria)
{
	auto table = hashTable();
	for (int i = 0; i < HashTableSize; i++)
	{
		UFlag* prev = nullptr;
		UFlag* flag = table[i];
		while (flag)
		{
			UFlag* next = flag->nextFlag();
			if (flag->expiration() != 0 && flag->expiration() <= criteria)
			{
				if (prev)
					prev->nextFlag() = next;
				else
					table[i] = next;
			}
			else
			{
				prev = flag;
			}
			flag = next;
		}
	}
}

bool UFlagBase::CheckFlag(const NameString& FlagName, uint8_t flagType)
{
	return GetFlag(FlagName, flagType) != nullptr;
}

int UFlagBase::CreateIterator(std::optional<uint8_t> flagType)
{
	int handle = NextIterator++;
	FlagIterator flagIt;
	if (flagType)
	{
		flagIt.FlagType = *flagType;
		flagIt.FlagTypeSet = true;
	}
	FlagIterators[handle] = flagIt;
	return handle;
}

void UFlagBase::DestroyIterator(int Iterator)
{
	FlagIterators.erase(Iterator);
}

bool UFlagBase::GetNextFlag(int Iterator, NameString& FlagName, uint8_t& flagType)
{
	FlagName = {}; // ResetFlags() expects this to be set to '' if no more flags were found

	auto it = FlagIterators.find(Iterator);
	if (it == FlagIterators.end())
		return false;

	auto table = hashTable();
	FlagIterator& flagIt = it->second;

	while (flagIt.HashPos < HashTableSize)
	{
		UFlag* flag = flagIt.NextFlag ? flagIt.NextFlag : table[flagIt.HashPos];
		while (flag && flagIt.FlagTypeSet && flag->flagType() != flagIt.FlagType)
			flag = flag->nextFlag();
		if (flag)
		{
			FlagName = flag->FlagName();
			flagType = flag->flagType();
			flagIt.NextFlag = flag->nextFlag();
			if (!flagIt.NextFlag)
				flagIt.HashPos++;
			return true;
		}
		flagIt.NextFlag = nullptr;
		flagIt.HashPos++;
	}
	return false;
}

bool UFlagBase::GetNextFlagName(int Iterator, NameString& FlagName)
{
	uint8_t flagType = 0;
	return GetNextFlag(Iterator, FlagName, flagType);
}

bool UFlagBase::GetBool(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Bool))
		return UObject::Cast<UFlagBool>(flag)->bValue();
	return false;
}

uint8_t UFlagBase::GetByte(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Byte))
		return UObject::Cast<UFlagByte>(flag)->byteValue();
	return 0;
}

int UFlagBase::GetExpiration(const NameString& FlagName, uint8_t flagType)
{
	// -1 for a flag that is not there, as the original answers.
	if (UFlag* flag = GetFlag(FlagName, flagType))
		return flag->expiration();
	return -1;
}

float UFlagBase::GetFloat(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Float))
		return UObject::Cast<UFlagFloat>(flag)->floatValue();
	return 0.0f;
}

int UFlagBase::GetInt(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Int))
		return UObject::Cast<UFlagInt>(flag)->intValue();
	return 0;
}

NameString UFlagBase::GetName(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Name))
		return UObject::Cast<UFlagName>(flag)->nameValue();
	return {};
}

Rotator UFlagBase::GetRotator(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Rotator))
		return UObject::Cast<UFlagRotator>(flag)->rotatorValue();
	return {};
}

vec3 UFlagBase::GetVector(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Vector))
		return UObject::Cast<UFlagVector>(flag)->vectorValue();
	return vec3(0.0f);
}

void UFlagBase::SetDefaultExpiration(int expiration)
{
	defaultFlagExpiration() = expiration;
}

void UFlagBase::SetExpiration(const NameString& FlagName, uint8_t flagType, int expiration)
{
	if (UFlag* flag = GetFlag(FlagName, flagType))
		flag->expiration() = expiration;
}

template<typename T>
T* UFlagBase::GetOrCreateFlag(const NameString& FlagName, std::optional<bool> bAdd, std::optional<int> expiration, EFlagType flagType, const char* flagClassName)
{
	// The expiration is stamped each set: the one given, or the flag base's
	// default for -1 (the script's own default argument).
	int newExpiration = (!expiration || *expiration == -1) ? defaultFlagExpiration() : *expiration;

	// Try get the flag
	if (auto flag = UObject::Cast<T>(GetFlag(FlagName, (uint8_t)flagType)))
	{
		flag->expiration() = newExpiration;
		return flag;
	}

	// Flag didn't exist. Create the flag, if requested
	if (!bAdd || *bAdd)
	{
		size_t classIndex = (size_t)flagType;
		if (classIndex >= FlagClasses.size())
			FlagClasses.resize(classIndex + 1);

		UClass*& cls = FlagClasses[classIndex];
		if (!cls)
		{
			cls = engine->packages->FindClass(flagClassName);
			if (cls == nullptr)
				throw std::runtime_error(std::string("Could not find class ") + flagClassName);
		}

		const uint32_t crc = FlagNameCrc(FlagName);
		const int bucket = (int)(crc % HashTableSize);
		auto flag = UObject::Cast<T>(engine->packages->GetTransientPackage()->NewObject(FlagName, cls, ObjectFlags::Transient));
		flag->FlagName() = FlagName;
		flag->FlagBase() = this;
		flag->flagType() = (uint8_t)flagType; // the class defaults leave it 0, Bool
		flag->flagHash() = (int)crc;
		flag->expiration() = newExpiration;

		// The chain is kept in order of hash, then type.
		auto table = hashTable();
		UFlag* prev = nullptr;
		for (UFlag* it = table[bucket]; it; prev = it, it = it->nextFlag())
		{
			if ((uint32_t)it->flagHash() > crc ||
				((uint32_t)it->flagHash() == crc && it->flagType() > (uint8_t)flagType))
				break;
		}
		if (prev)
		{
			flag->nextFlag() = prev->nextFlag();
			prev->nextFlag() = flag;
		}
		else
		{
			flag->nextFlag() = table[bucket];
			table[bucket] = flag;
		}
		return flag;
	}

	return nullptr;
}

bool UFlagBase::SetBool(const NameString& FlagName, bool NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagBool>(FlagName, bAdd, expiration, EFlagType::Bool, "Extension.FlagBool"))
	{
		flag->bValue() = NewValue;
		return true;
	}
	return false;
}

bool UFlagBase::SetByte(const NameString& FlagName, uint8_t NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagByte>(FlagName, bAdd, expiration, EFlagType::Byte, "Extension.FlagByte"))
	{
		flag->byteValue() = NewValue;
		return true;
	}
	return false;
}

bool UFlagBase::SetFloat(const NameString& FlagName, float NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagFloat>(FlagName, bAdd, expiration, EFlagType::Float, "Extension.FlagFloat"))
	{
		flag->floatValue() = NewValue;
		return true;
	}
	return false;
}

bool UFlagBase::SetInt(const NameString& FlagName, int NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagInt>(FlagName, bAdd, expiration, EFlagType::Int, "Extension.FlagInt"))
	{
		flag->intValue() = NewValue;
		return true;
	}
	return false;
}

bool UFlagBase::SetName(const NameString& FlagName, const NameString& NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagName>(FlagName, bAdd, expiration, EFlagType::Name, "Extension.FlagName"))
	{
		flag->nameValue() = NewValue;
		return true;
	}
	return false;
}

bool UFlagBase::SetRotator(const NameString& FlagName, const Rotator& NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagRotator>(FlagName, bAdd, expiration, EFlagType::Rotator, "Extension.FlagRotator"))
	{
		flag->rotatorValue() = NewValue;
		return true;
	}
	return false;
}

bool UFlagBase::SetVector(const NameString& FlagName, const vec3& NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagVector>(FlagName, bAdd, expiration, EFlagType::Vector, "Extension.FlagVector"))
	{
		flag->vectorValue() = NewValue;
		return true;
	}
	return false;
}
