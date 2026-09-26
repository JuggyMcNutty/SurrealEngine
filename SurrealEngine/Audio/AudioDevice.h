#pragma once

#include "AudioSource.h"

#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include "Math/vec.h"

class AudioSource;
class USound;

// A zone's reverb, as Galaxy takes it (galaxy-dll.md, Reverb): the master
// gain, the high-frequency cutoff, and six echoes.
struct ReverbSettings
{
	float masterGain = 0.0f;        // 0-1
	float cutoffHz = 44100.0f;
	float delaySeconds[6] = {};     // each echo's delay
	float gains[6] = {};            // each echo's gain, 0-1
};

class AudioDevice
{
public:
	static std::unique_ptr<AudioDevice> Create(int frequency, int numVoices, int musicBufferCount, int musicBufferSize);

	virtual ~AudioDevice() = default;
	virtual void AddSound(USound* sound) = 0;
	virtual void RemoveSound(USound* sound) = 0;
	virtual bool IsPlaying(int channel) = 0;
	virtual int GetTotalChannels() = 0;
	virtual void PlaySound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch, bool speech = false) = 0;
	virtual void PlayMusic(std::unique_ptr<AudioSource> source) = 0;
	virtual void UpdateSound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) = 0;
	virtual void StopSound(int channel) = 0;
	virtual void SetMusicVolume(float volume) = 0;
	virtual void SetSoundVolume(float volume) = 0;
	virtual void SetSpeechVolume(float volume) = 0;
	// The playing music's order (UE1's SongSection), -1 while unknown, and a
	// jump to another order without reloading the song.
	virtual int GetMusicOrder() = 0;
	virtual void SetMusicOrder(int order) = 0;
	// The reverb every sound plays through; nullptr turns it off.
	virtual void SetReverb(const ReverbSettings* settings) = 0;
	virtual void Update() = 0;
};
