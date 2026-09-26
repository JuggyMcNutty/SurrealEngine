
#include "Precomp.h"
#include "USurrealAudioDevice.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/IniProperty.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/UMusic.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Collision/BottomLevel/TraceRayModel.h"

static float square(float x) { return x * x; }

// The light's momentary animation scalar, the same shapes the renderer's
// LightmapBuilder::GetLightColor gives a lightmap (a steady light is 1): the
// original scales an ambient sound on a light by the renderer's own
// GlobalLighting (galaxy-dll.md, Each frame). The palette types stay 1.
static float AmbientLightScalar(UActor* light)
{
	constexpr float phaseScale = (1.0f / 255.0f);
	constexpr float periodSpeed = 40.0f;
	constexpr float turnsToRadians = 2.0f * 3.14159265359f;
	constexpr float strobeSpeed = 10.0f;
	switch (light->LightType())
	{
	default:
		return 1.0f;
	case LT_Pulse:
	{
		float pulseTurns = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		return 0.65f + 0.35f * std::sin(pulseTurns * turnsToRadians);
	}
	case LT_SubtlePulse:
	{
		float pulseTurns = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		return 0.8f + 0.2f * std::sin(pulseTurns * turnsToRadians);
	}
	case LT_Blink:
		return std::fmod(light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1), 2.0f) < 1.0f ? 1.0f : 0.0f;
	case LT_Strobe:
		return std::fmod(light->Level()->TimeSeconds() * strobeSpeed, 2.0f) < 1.0f ? 1.0f : 0.0f;
	case LT_Flicker:
		return light->Light.FlickerRandom ? 1.0f : 0.0f;
	}
}

std::string USurrealAudioDevice::GetPropertyAsString(const NameString& propertyName) const
{
	if (propertyName == "Class")
		return "class'" + Class + "'";
	else if (propertyName == "UseFilter")
		return IniPropertyConverter<bool>::ToString(UseFilter);
	else if (propertyName == "UseSurround")
		return IniPropertyConverter<bool>::ToString(UseSurround);
	else if (propertyName == "UseStereo")
		return IniPropertyConverter<bool>::ToString(UseStereo);
	else if (propertyName == "UseCDMusic")
		return IniPropertyConverter<bool>::ToString(UseCDMusic);
	else if (propertyName == "UseDigitalMusic")
		return IniPropertyConverter<bool>::ToString(UseDigitalMusic);
	else if (propertyName == "UseSpatial")
		return IniPropertyConverter<bool>::ToString(UseSpatial);
	else if (propertyName == "UseReverb")
		return IniPropertyConverter<bool>::ToString(UseReverb);
	else if (propertyName == "Use3dHardware")
		return IniPropertyConverter<bool>::ToString(Use3dHardware);
	else if (propertyName == "LowSoundQuality")
		return IniPropertyConverter<bool>::ToString(LowSoundQuality);
	else if (propertyName == "ReverseStereo")
		return IniPropertyConverter<bool>::ToString(ReverseStereo);
	else if (propertyName == "Latency")
		return IniPropertyConverter<int>::ToString(Latency);
	else if (propertyName == "OutputRate")
		return IniPropertyConverter<AudioFrequency>::ToString(OutputRate);
	else if (propertyName == "Channels")
		return IniPropertyConverter<int>::ToString(Channels);
	else if (propertyName == "MusicVolume")
		return IniPropertyConverter<uint8_t>::ToString(MusicVolume);
	else if (propertyName == "SoundVolume")
		return IniPropertyConverter<uint8_t>::ToString(SoundVolume);
	else if (propertyName == "SpeechVolume")
		return IniPropertyConverter<uint8_t>::ToString(SpeechVolume);
	else if (propertyName == "AmbientFactor")
		return IniPropertyConverter<float>::ToString(AmbientFactor);
	else if (propertyName == "DopplerSpeed")
		return IniPropertyConverter<float>::ToString(DopplerSpeed);

	LogMessage("Queried unknown property for SurrealAudioDevice: " + propertyName.ToString());
	return {};
}

void USurrealAudioDevice::SetPropertyFromString(const NameString& propertyName, const std::string& value)
{
	if (propertyName == "UseFilter")
		UseFilter = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "UseSurround")
		UseSurround = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "UseStereo")
		UseStereo = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "UseCDMusic")
		UseCDMusic = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "UseDigitalMusic")
		UseDigitalMusic = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "UseSpatial")
		UseSpatial = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "UseReverb")
		UseReverb = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "Use3dHardware")
		Use3dHardware = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "LowSoundQuality")
		LowSoundQuality = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "ReverseStereo")
		ReverseStereo = IniPropertyConverter<bool>::FromString(value);
	else if (propertyName == "Latency")
		Latency = IniPropertyConverter<int>::FromString(value);
	else if (propertyName == "OutputRate")
		OutputRate = IniPropertyConverter<AudioFrequency>::FromString(value);
	else if (propertyName == "Channels")
		Channels = IniPropertyConverter<int>::FromString(value);
	else if (propertyName == "MusicVolume")
		MusicVolume = IniPropertyConverter<uint8_t>::FromString(value);
	else if (propertyName == "SoundVolume")
		SoundVolume = IniPropertyConverter<uint8_t>::FromString(value);
	else if (propertyName == "SpeechVolume")
		SpeechVolume = IniPropertyConverter<uint8_t>::FromString(value);
	else if (propertyName == "AmbientFactor")
		AmbientFactor = IniPropertyConverter<float>::FromString(value);
	else if (propertyName == "DopplerSpeed")
		DopplerSpeed = IniPropertyConverter<float>::FromString(value);
	else
		LogMessage("Setting unknown property for SurrealAudioDevice: " + propertyName.ToString());

	engine->packages->SetIniValue("System", Class, propertyName, value);
}

void USurrealAudioDevice::LoadProperties(const NameString& from)
{
	NameString name_from = from;

	if (from == "")
		name_from = NameString(Class);

	UseFilter = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseFilter", UseFilter);
	UseSurround = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseSurround", UseSurround);
	UseStereo = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseStereo", UseStereo);
	UseCDMusic = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseCDMusic", UseCDMusic);
	UseDigitalMusic = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseDigitalMusic", UseDigitalMusic);
	UseSpatial = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseSpatial", UseSpatial);
	UseReverb = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "UseReverb", UseReverb);
	Use3dHardware = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "Use3dHardware", Use3dHardware);
	LowSoundQuality = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "LowSoundQuality", LowSoundQuality);
	ReverseStereo = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "ReverseStereo", ReverseStereo);
	Latency = IniPropertyConverter<int>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "Latency", Latency);
	OutputRate = IniPropertyConverter<AudioFrequency>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "OutputRate", OutputRate);
	Channels = IniPropertyConverter<int>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "Channels", Channels);
	MusicVolume = IniPropertyConverter<uint8_t>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "MusicVolume", MusicVolume);
	SoundVolume = IniPropertyConverter<uint8_t>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "SoundVolume", SoundVolume);
	SpeechVolume = IniPropertyConverter<uint8_t>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "SpeechVolume", SpeechVolume);
	AmbientFactor = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "AmbientFactor", AmbientFactor);
	DopplerSpeed = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("System"), name_from, "DopplerSpeed", DopplerSpeed);
}

void USurrealAudioDevice::SaveConfig()
{
	engine->packages->SetIniValue("System", Class, "UseFilter", IniPropertyConverter<bool>::ToString(UseFilter));
	engine->packages->SetIniValue("System", Class, "UseSurround", IniPropertyConverter<bool>::ToString(UseSurround));
	engine->packages->SetIniValue("System", Class, "UseStereo", IniPropertyConverter<bool>::ToString(UseStereo));
	engine->packages->SetIniValue("System", Class, "UseCDMusic", IniPropertyConverter<bool>::ToString(UseCDMusic));
	engine->packages->SetIniValue("System", Class, "UseDigitalMusic", IniPropertyConverter<bool>::ToString(UseDigitalMusic));
	engine->packages->SetIniValue("System", Class, "UseSpatial", IniPropertyConverter<bool>::ToString(UseSpatial));
	engine->packages->SetIniValue("System", Class, "UseReverb", IniPropertyConverter<bool>::ToString(UseReverb));
	engine->packages->SetIniValue("System", Class, "Use3dHardware", IniPropertyConverter<bool>::ToString(Use3dHardware));
	engine->packages->SetIniValue("System", Class, "LowSoundQuality", IniPropertyConverter<bool>::ToString(LowSoundQuality));
	engine->packages->SetIniValue("System", Class, "ReverseStereo", IniPropertyConverter<bool>::ToString(ReverseStereo));
	engine->packages->SetIniValue("System", Class, "Latency", IniPropertyConverter<int>::ToString(Latency));
	engine->packages->SetIniValue("System", Class, "OutputRate", IniPropertyConverter<AudioFrequency>::ToString(OutputRate));
	engine->packages->SetIniValue("System", Class, "Channels", IniPropertyConverter<int>::ToString(Channels));
	engine->packages->SetIniValue("System", Class, "MusicVolume", IniPropertyConverter<uint8_t>::ToString(MusicVolume));
	engine->packages->SetIniValue("System", Class, "SoundVolume", IniPropertyConverter<uint8_t>::ToString(SoundVolume));
	engine->packages->SetIniValue("System", Class, "SpeechVolume", IniPropertyConverter<uint8_t>::ToString(SpeechVolume));
	engine->packages->SetIniValue("System", Class, "AmbientFactor", IniPropertyConverter<float>::ToString(AmbientFactor));
	engine->packages->SetIniValue("System", Class, "DopplerSpeed", IniPropertyConverter<float>::ToString(DopplerSpeed));
}

void USurrealAudioDevice::InitDevice()
{
	// TODO: Add configurable option for audio device
	// TODO: Add configurable option for audio output frequency
	// TODO: Add option for number of sound channels
	// TODO: Add option for music buffer count
	// TODO: Add option for music buffer size
	// m_Device = AudioDevice::Create(48000, 256, 16, 256);
	m_Device = AudioDevice::Create(OutputRate.frequency, 256, 16, 256);
	LogMessage("Audio device initialized");
}

void USurrealAudioDevice::ShutdownDevice()
{
	m_Device.reset();
	LogMessage("Audio device has been shut down");
}

void USurrealAudioDevice::SetViewport(UViewport* InViewport)
{
	if (m_Viewport != InViewport)
	{
		StopSounds();

		m_Viewport = InViewport;

		if (m_Viewport)
		{
			if (m_Viewport->Actor()->Song() && m_Viewport->Actor()->Transition() == MTRAN_None)
				m_Viewport->Actor()->Transition() = MTRAN_Instant;

			PlayingSounds.resize(std::min(Channels, m_Device->GetTotalChannels()));
		}
	}
}

void USurrealAudioDevice::Update(const mat4& listener)
{
	// The update's own time step, 0 to 1 s, as Galaxy's (galaxy-dll.md, Each
	// frame); the obstruction fade runs on it.
	auto now = std::chrono::steady_clock::now();
	float timeStep = std::clamp(std::chrono::duration<float>(now - m_LastUpdateTime).count(), 0.0f, 1.0f);
	m_LastUpdateTime = now;

	StartAmbience();
	UpdateAmbience();
	UpdateSounds(listener, timeStep);
	UpdateMusic(timeStep);
	UpdateReverb();

	float musicFade = 1.0f;
	if (m_MusicTransition && m_MusicFadeLength > 0.0f)
		musicFade = std::max(m_MusicFadeLeft, 0.0f) / m_MusicFadeLength;
	m_Device->SetMusicVolume(MusicVolume / 255.0f * musicFade);
	if (engine->LaunchInfo.IsDeusEx())
	{
		// Each sound plays at its own slider, speech at the Speech slider, as the
		// original's (galaxy-dll.md, Volume). Galaxy's equal-sliders quirk -- both
		// scaled by the slider twice -- is not carried.
		m_Device->SetSoundVolume(SoundVolume / 255.0f);
		m_Device->SetSpeechVolume(SpeechVolume / 255.0f);
	}
	else
	{
		// Other games keep the halving their normalized volumes were tuned against,
		// speech at the Sound slider with the rest.
		m_Device->SetSoundVolume(SoundVolume / 255.0f * 0.5f);
		m_Device->SetSpeechVolume(SoundVolume / 255.0f * 0.5f);
	}
	m_Device->Update();
}

void USurrealAudioDevice::StartAmbience()
{
	if (!m_Viewport || !m_Viewport->Actor())
		return;

	bool Realtime = m_Viewport->IsRealtime() && m_Viewport->Actor()->Level()->Pauser() == "";
	if (Realtime)
	{
		UActor* ViewActor = m_Viewport->Actor()->ViewTarget() ? m_Viewport->Actor()->ViewTarget() : m_Viewport->Actor();
		int actorIndex = 0;
		for (UActor* Actor : m_Viewport->Actor()->XLevel()->Actors)
		{
			if (Actor && Actor->AmbientSound() && dist_squared(ViewActor->Location(), Actor->Location()) <= square(Actor->WorldSoundRadius()))
			{
				int Id = actorIndex * 16 + SLOT_Ambient * 2;
				bool foundSound = false;
				for (size_t j = 0; j < PlayingSounds.size(); j++)
				{
					if (PlayingSounds[j].Id == Id)
					{
						foundSound = true;
						break;
					}
				}
				if (!foundSound)
					PlaySound(Actor, Id, Actor->AmbientSound(), Actor->Location(), AmbientFactor * Actor->SoundVolume() / 255.0f, Actor->WorldSoundRadius(), Actor->SoundPitch() / 64.0f, false);
			}
			actorIndex++;
		}
	}
}

void USurrealAudioDevice::UpdateAmbience()
{
	if (!m_Viewport || !m_Viewport->Actor())
		return;

	UActor* ViewActor = m_Viewport->Actor()->ViewTarget() ? m_Viewport->Actor()->ViewTarget() : m_Viewport->Actor();
	bool Realtime = m_Viewport->IsRealtime() && m_Viewport->Actor()->Level()->Pauser() == "";
	for (size_t i = 0; i < PlayingSounds.size(); i++)
	{
		PlayingSound& Playing = PlayingSounds[i];
		if ((Playing.Id & 14) == SLOT_Ambient * 2)
		{
			if (Playing.Actor->bDeleteMe() || dist_squared(ViewActor->Location(), Playing.Actor->Location()) > square(Playing.Actor->WorldSoundRadius()) || Playing.Actor->AmbientSound() != Playing.Sound || !Realtime)
			{
				// Ambient sound went out of range
				StopSound(i);
			}
			else
			{
				// Update basic sound properties
				Playing.Volume = 2.0f * (AmbientFactor * Playing.Actor->SoundVolume() / 255.0f);
				Playing.Radius = Playing.Actor->WorldSoundRadius();
				Playing.Pitch = Playing.Actor->SoundPitch() / 64.0f;

				// An actor with a light has its sound follow the light: the volume
				// times LightBrightness / 255 and the light's momentary pulse or
				// flicker, then at most 1 (galaxy-dll.md, Each frame).
				if (engine->LaunchInfo.IsDeusEx() && Playing.Actor->LightType() != LT_None)
					Playing.Volume = std::min(Playing.Volume * (Playing.Actor->LightBrightness() / 255.0f) * AmbientLightScalar(Playing.Actor), 1.0f);

				// Deus Ex's Doppler is the ambient sound's alone: the pitch times
				// 1 - the actor's speed away from the view target / DopplerSpeed,
				// kept to 0.5-2 (galaxy-dll.md, Each frame). AL's own Doppler is
				// off for Deus Ex, so nothing else shifts.
				if (engine->LaunchInfo.IsDeusEx() && DopplerSpeed > 0.0f)
				{
					vec3 away = Playing.Actor->Location() - ViewActor->Location();
					float distance = length(away);
					if (distance > 0.0f)
					{
						float speedAway = dot(Playing.Actor->Velocity(), away / distance);
						Playing.Pitch *= std::clamp(1.0f - speedAway / DopplerSpeed, 0.5f, 2.0f);
					}
				}
			}
		}
	}
}

//
// Deliberately stores no state on PlayingSound. The slot is already packed into Id by
// NActor::PlaySound as (slot << 1), and this file already decodes it that way for
// SLOT_Ambient, so SLOT_Talk is recoverable the same way.
//
// PropertyOffsets only resolves the lip-sync properties inside its IsDeusEx() branch, so an
// unset offset doubles as the "this game has no lip sync" test: other games return on the
// first line and observe no behaviour change at all.
static bool HasLipSync()
{
	return PropOffsets_Pawn.nextPhoneme.DataOffset != ~(size_t)0;
}

void USurrealAudioDevice::UpdateLipSync(PlayingSound& Playing)
{
	if (!HasLipSync() || !Playing.Actor || !engine->LevelInfo)
		return;
	if ((Playing.Id & 14) != SLOT_Talk * 2)
		return;

	UPawn* pawn = UObject::TryCast<UPawn>(Playing.Actor);
	if (!pawn)
		return;

	// bIsSpeaking is the script's alone: ConPlay sets it around a line and
	// LipSynch's bWasSpeaking branch closes the mouth when it drops, so the
	// original's Galaxy only reads it (galaxy-dll.md, Lip sync). Writing it
	// here moved mouths on barks the script never opened.
	if (!pawn->bIsSpeaking())
		return;

	float elapsedTime = engine->LevelInfo->TimeSeconds() - Playing.StartTime;
	uint8_t letter = Playing.Sound->GetLipsyncLetterAt(elapsedTime);

	if (!letter)
		return;

	pawn->nextPhoneme() = std::string(1, letter);
}

void USurrealAudioDevice::UpdateSounds(const mat4& listener, float timeStep)
{
	if (!m_Viewport || !m_Viewport->Actor())
		return;

	UActor* ViewActor = m_Viewport->Actor()->ViewTarget() ? m_Viewport->Actor()->ViewTarget() : m_Viewport->Actor();
	for (size_t i = 0; i < PlayingSounds.size(); i++)
	{
		PlayingSound& Playing = PlayingSounds[i];

		if (Playing.Id != 0)
		{
			// Update positioning from actor, if available
			if (Playing.Actor)
				Playing.Location = Playing.Actor->Location();

			// Update the priority
			Playing.Priority = SoundPriority(m_Viewport, Playing.Location, Playing.Volume, Playing.Radius);

			UpdateLipSync(Playing);

			// Deus Ex: a wall between the player's eyes and the sound fades it
			// toward a third of its volume over half a second, and back as it
			// clears (galaxy-dll.md, Sounds behind walls).
			float volume = Playing.Volume;
			if (engine->LaunchInfo.IsDeusEx())
			{
				UpdateObstruction(Playing, timeStep);
				volume *= std::max(1.0f - 2.0f * Playing.ObstructionTime, 0.33f);
			}

			// Update the sound.
			if (Playing.IsActive)
			{
				if (m_Device->IsPlaying((int)i))
				{
					m_Device->UpdateSound((int)i, Playing.Sound, Playing.Location, volume, Playing.Radius, Playing.Pitch);
				}
				else
				{
					PlayingSounds[i] = {};
				}
			}
			else
			{
				m_Device->PlaySound((int)i, Playing.Sound, Playing.Location, volume, Playing.Radius, Playing.Pitch, (Playing.Id & 14) == SLOT_Talk * 2);
				Playing.IsActive = true;
			}
		}
	}
}

void USurrealAudioDevice::UpdateObstruction(PlayingSound& Playing, float timeStep)
{
	// Speech is never muffled, nor a sound whose actor has gone.
	if (!Playing.Actor || (Playing.Id & 14) == SLOT_Talk * 2)
	{
		Playing.ObstructionTime = 0.0f;
		return;
	}

	// The line runs from the player's own eyes -- the player's place and
	// EyeHeight, even when viewing through another actor -- to the sound's
	// actor, and only the level's BSP blocks it: movers and actors do not,
	// as the original's UModel::FastLineCheck has it.
	UActor* player = m_Viewport->Actor();
	vec3 eyes = player->Location();
	if (UPawn* pawn = UObject::TryCast<UPawn>(player))
		eyes.z += pawn->EyeHeight();

	bool blocked = false;
	dvec3 origin = to_dvec3(eyes);
	dvec3 direction = to_dvec3(Playing.Location) - origin;
	double tmax = length(direction);
	if (tmax > 0.01)
	{
		TraceRayModel trace;
		blocked = trace.TraceAnyHit(player->XLevel()->Model, origin, 0.01, direction * (1.0 / tmax), tmax, true);
	}

	if (blocked)
		Playing.ObstructionTime = std::min(Playing.ObstructionTime + timeStep, 0.5f);
	else
		Playing.ObstructionTime = std::max(Playing.ObstructionTime - timeStep, 0.0f);
}

// Builds the source for a song; a tracker module starts at the given order.
static std::unique_ptr<AudioSource> CreateMusicSource(UMusic* song, int order)
{
	std::unique_ptr<AudioSource> source;
	if (song->Format == "mp3" || song->Format == "mp2")
		source = AudioSource::CreateMp3(song->Data);
	else if (song->Format == "ogg" || song->Format == "event") // Some ogg files in Unreal 227 have event as format for some reason
		source = AudioSource::CreateOgg(song->Data, true);
	else if (song->Format == "wav")
		source = AudioSource::CreateWav(song->Data);
	else
	{
		source = AudioSource::CreateMod(song->Data, true, 0);
		if (source && order > 0)
			source->SetOrder(order);
	}
	return source;
}

void USurrealAudioDevice::UpdateMusic(float timeStep)
{
	if (!m_Viewport || !m_Viewport->Actor())
		return;

	if (!engine->LaunchInfo.IsDeusEx())
	{
		// Other games keep the fork's instant switch.
		if (m_Viewport->Actor()->Transition() != MTRAN_None)
		{
			if (CurrentSong)
			{
				m_Device->PlayMusic({});
				CurrentSong = nullptr;
			}

			CurrentSong = m_Viewport->Actor()->Song();
			CurrentSection = m_Viewport->Actor()->SongSection();

			if (CurrentSong && UseDigitalMusic)
			{
				auto source = CreateMusicSource(CurrentSong, CurrentSection != 255 ? CurrentSection : 0);
				if (source)
					m_Device->PlayMusic(std::move(source));
			}

			m_Viewport->Actor()->Transition() = MTRAN_None;
		}
		return;
	}

	auto* player = m_Viewport->Actor();

	if (!m_MusicTransition && player->Transition() != MTRAN_None)
	{
		// The playing music fades out first: 1 s for MTRAN_Fade, 5 s for
		// MTRAN_SlowFade, 1/3 s for MTRAN_FastFade, at once for the others,
		// plus twice Latency (galaxy-dll.md, Music).
		float length = 0.0f;
		switch (player->Transition())
		{
		case MTRAN_Fade: length = 1.0f; break;
		case MTRAN_SlowFade: length = 5.0f; break;
		case MTRAN_FastFade: length = 1.0f / 3.0f; break;
		default: break;
		}
		if (length > 0.0f)
			length += 2.0f * Latency / 1000.0f;
		if (!CurrentSong)
			length = 0.0f;   // nothing plays, nothing to fade
		m_MusicTransition = true;
		m_MusicFadeLength = length;
		m_MusicFadeLeft = length;
	}

	if (m_MusicTransition)
	{
		m_MusicFadeLeft -= timeStep;
		if (m_MusicFadeLeft > 0.0f)
			return;

		// The fade is done: the player's Song starts at full volume at the
		// order SongSection -- a different song is loaded, the same one only
		// jumps -- and section 255 is silence (galaxy-dll.md, Music).
		UMusic* song = player->Song();
		int section = player->SongSection();
		if (!song || section == 255 || !UseDigitalMusic)
		{
			m_Device->PlayMusic({});
			CurrentSong = nullptr;
		}
		else if (song == CurrentSong)
		{
			m_Device->SetMusicOrder(section);
		}
		else
		{
			auto source = CreateMusicSource(song, section);
			m_Device->PlayMusic(std::move(source));
			CurrentSong = song;
		}
		CurrentSection = section;
		m_MusicTransition = false;
		m_MusicFadeLength = 0.0f;
		m_MusicFadeLeft = 0.0f;
		player->Transition() = MTRAN_None;
	}
	else if (CurrentSong)
	{
		// Where the music is: while no transition waits, each frame writes the
		// order playing back into the player's SongSection, so the ambient
		// track comes back where it was after a fight or a conversation
		// (galaxy-dll.md, Music).
		int order = m_Device->GetMusicOrder();
		if (order >= 0)
		{
			player->SongSection() = (uint8_t)order;
			CurrentSection = order;
		}
	}
}

void USurrealAudioDevice::UpdateReverb()
{
	if (!engine->LaunchInfo.IsDeusEx() || !m_Viewport || !m_Viewport->Actor())
		return;

	// With UseReverb, the zone of the player's view target with bReverbZone
	// gives every sound its reverb -- MasterGain / 255 the volume, CutoffHz
	// the damping of highs, six echoes of Delay x 2 ms at Gain / 255 --
	// another zone gives none, and it is set again only when it changes
	// (galaxy-dll.md, Reverb).
	UActor* ViewActor = m_Viewport->Actor()->ViewTarget() ? m_Viewport->Actor()->ViewTarget() : m_Viewport->Actor();
	UZoneInfo* zone = ViewActor->Region().Zone;
	UZoneInfo* reverbZone = (UseReverb && zone && zone->bReverbZone()) ? zone : nullptr;
	if (reverbZone == m_ReverbZone)
		return;
	m_ReverbZone = reverbZone;

	if (!reverbZone)
	{
		m_Device->SetReverb(nullptr);
		return;
	}

	ReverbSettings settings;
	settings.masterGain = reverbZone->MasterGain() / 255.0f;
	settings.cutoffHz = (float)std::min(reverbZone->CutoffHz(), 44100);
	for (int i = 0; i < 6; i++)
	{
		settings.delaySeconds[i] = std::clamp(reverbZone->Delay()[i] * 2, 1, 340) / 1000.0f;
		settings.gains[i] = reverbZone->Gain()[i] / 255.0f;
	}
	m_Device->SetReverb(&settings);
}

bool USurrealAudioDevice::PlaySound(UActor* Actor, int Id, USound* Sound, vec3 Location, float Volume, float Radius, float Pitch, bool isTalk)
{
	if (Radius <= 0.0) // Seems we have zero radius values. Lovely.
		Radius = 1500.0f;

	if (!engine->LaunchInfo.IsDeusEx())
	{
		// Attempt to normalize volume around 1.0 as the values used by the original games are just really broken in general.
		if (Volume >= 8.0f)
			Volume = 0.8f; // Special check for announcer garbage
		else
			Volume = (Volume - 1.0f) * 0.25f + 1.0f;
	}
	// Deus Ex plays the script's volume as Galaxy does: scaled only by fall-off,
	// obstruction and the sliders, and capped at full by the device (AL_MAX_GAIN).

	if (!m_Viewport || !Sound)
		return false;

	// Allocate a new slot if requested
	if ((Id & 14) == 2 * SLOT_None)
		Id = 16 * --FreeSlot;

	float Priority = SoundPriority(m_Viewport, Location, Volume, Radius);

	// If already playing, stop it
	size_t Index = PlayingSounds.size();
	float BestPriority = Priority;
	for (size_t i = 0; i < PlayingSounds.size(); i++)
	{
		PlayingSound& Playing = PlayingSounds[i];
		if ((Playing.Id & ~1) == (Id & ~1))
		{
			// Skip if not interruptable.
			if (Id & 1)
				return 0;

			// Stop the sound.
			Index = i;
			break;
		}
		else if (Playing.Priority <= BestPriority)
		{
			Index = i;
			BestPriority = Playing.Priority;
		}
	}

	// If no sound, or its priority is overruled, stop it
	if (Index == PlayingSounds.size())
		return 0;

	Sound->GetSound();

	// Put the sound on the play-list
	StopSound(Index);
	PlayingSounds[Index] = PlayingSound(Actor, Id, Sound, Location, Volume, Radius, Pitch, Priority);
	PlayingSounds[Index].StartTime = engine->LevelInfo->TimeSeconds();

	return true;
}

void USurrealAudioDevice::ActorDestroyed(UActor* Actor)
{
	for (size_t i = 0; i < PlayingSounds.size(); i++)
	{
		if (PlayingSounds[i].Actor == Actor)
		{
			if ((PlayingSounds[i].Id & 14) == SLOT_Ambient * 2)
			{
				// Stop ambient sound when actor dies
				StopSound(i);
			}
			else
			{
				// Unbind regular sounds from actors
				PlayingSounds[i].Actor = nullptr;
			}
		}
	}
}

void USurrealAudioDevice::StopSound(UActor* Actor, int Id)
{
	for (size_t i = 0; i < PlayingSounds.size(); i++)
	{
		if (PlayingSounds[i].Actor == Actor && PlayingSounds[i].Id == Id)
		{
			StopSound(i);
			break;
		}
	}
}

void USurrealAudioDevice::StopSound(size_t index)
{
	PlayingSound& Playing = PlayingSounds[index];

	if (Playing.IsActive)
	{
		m_Device->StopSound((int)index);
		Playing.IsActive = false;
	}

	// The mouth is left as it is: the script's LipSynch closes it when ConPlay
	// drops bIsSpeaking, as the original does.
	PlayingSounds[index] = {};
}

void USurrealAudioDevice::StopSounds()
{
	for (size_t i = 0; i < PlayingSounds.size(); i++)
		StopSound(i);

	m_Device->PlayMusic(nullptr);
	m_Viewport = nullptr;
}

void USurrealAudioDevice::BreakpointTriggered()
{
	if (m_Device)
	{
		m_Device->SetSoundVolume(0.0f);
		m_Device->SetSpeechVolume(0.0f);
		m_Device->Update();
	}
}

void USurrealAudioDevice::AddStats(Array<std::string>& lines)
{
	const int bufsize = 1024;
	char buffer[bufsize];
	int index = 0;
	for (const PlayingSound& sound : PlayingSounds)
	{
		if (sound.IsActive)
		{
			std::snprintf(buffer, bufsize - 1, "Channel %2i: Vol: %05.2f %s", index, sound.Volume, sound.Sound->Name.ToString().c_str());
		}
		else
		{
			if (index >= 10)
				std::snprintf(buffer, bufsize - 1, "Channel %i:  None", index);
			else
				std::snprintf(buffer, bufsize - 1, "Channel %i: None", index);
		}
		buffer[bufsize - 1] = 0;
		lines.push_back(buffer);
		index++;
	}
}

float USurrealAudioDevice::SoundPriority(UViewport* Viewport, vec3 Location, float Volume, float Radius)
{
	UActor* target = Viewport->Actor();
	if (target && Viewport->Actor()->ViewTarget())
		target = Viewport->Actor()->ViewTarget();
	if (!target)
		return 0.0f;
	float priority = Volume * (1.0f - length(Location - target->Location()) / Radius);
	// Galaxy lets a sound beyond its radius go negative: it never beats an empty
	// channel's 0, so it is dropped even when one is free, and a playing sound
	// that left its radius is the first stolen (galaxy-dll.md, Playing a sound).
	// Other games keep the fork's floor, a free channel taken and held silent.
	return engine->LaunchInfo.IsDeusEx() ? priority : std::max(priority, 0.0f);
}
