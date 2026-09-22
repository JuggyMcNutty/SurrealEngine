#pragma once

#include "GameWindow.h"

class Engine;
struct GamepadState;

// Controller support.
//
// The pad is polled once per tick (DisplayBackend::GetGamepadState) and
// turned into what the engine already understands:
//
//  - In play, buttons are the UE1 joystick keys Joy1..Joy12 and JoyPov*, and
//    the sticks and triggers are the JoyX/Y/U/V/Z/R axes, so what they do is
//    whatever User.ini binds them to -- the same table as the keyboard.
//  - While Deus Ex's interface is up (a modal window: the main menu, the
//    inventory, a conversation, a keypad), the sticks move the pointer, A and
//    X click, B, Y, SELECT and START are Escape (so the button that opened a
//    screen closes it), the d-pad is the arrow keys and L1/R1 scroll.
//
// Button numbering follows the XInput order UE1 ports use: A=Joy1, B=Joy2,
// X=Joy3, Y=Joy4, L1=Joy5, R1=Joy6, SELECT=Joy7, START=Joy8, L3=Joy9,
// R3=Joy10, L2=Joy11, R2=Joy12 (triggers count as pressed past half travel).
//
// Axis values: a stick at full deflection sends 100. Up and right are
// positive. The engine multiplies axis input by 16 and then by the binding's
// Speed, so "JoyY=Axis aBaseY Speed=3.75" matches the keyboard's top speed.
class GamepadInput
{
public:
	void Update(Engine* engine, float timeElapsed);

	// True once when A, B, SELECT or START goes down: for skipping a movie,
	// whose playback loop reads no other input.
	bool PollSkip();

private:
	enum class Owner : uint8_t { None, Game, UI };
	enum { NumButtons = 15 + 2 };   // SDL's buttons, then the two triggers

	void Configure();
	void ReadButtons(const GamepadState& state, bool down[NumButtons]);
	void Press(Engine* engine, int button, bool ui);
	void Release(Engine* engine, int button);
	void UIRepeat(Engine* engine, float timeElapsed);
	void GameAxes(Engine* engine, const GamepadState& state);
	void ReleaseGameAxes(Engine* engine);
	void Pointer(Engine* engine, const GamepadState& state, float timeElapsed);

	bool configured = false;
	bool enabled = true;
	bool uiLast = false;

	bool wasDown[NumButtons] = {};
	Owner owner[NumButtons] = {};

	// Held d-pad and shoulder buttons repeat in menus, like a held key.
	int repeatButton = -1;
	float repeatWait = 0.0f;

	// Which game axes are currently non-zero, so each gets one release.
	bool axisActive[6] = {};

	// Sub-pixel pointer movement carried between ticks.
	float pointerRemX = 0.0f, pointerRemY = 0.0f;

	bool skipWasDown = false;
};
