#include "Precomp.h"
#include "GamepadInput.h"
#include "Engine.h"
#include "LauncherSettings.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include <surrealwidgets/window/window.h>
#include <algorithm>
#include <cmath>

namespace
{
	// Index = GamepadButton, then the two triggers.
	enum { TriggerLeft = (int)GamepadButton::Count, TriggerRight };

	const EInputKey GameKeys[15 + 2] =
	{
		IK_Joy1,        // A
		IK_Joy2,        // B
		IK_Joy3,        // X
		IK_Joy4,        // Y
		IK_Joy7,        // SELECT
		IK_None,        // MENU: the handheld's own menu button, not ours
		IK_Joy8,        // START
		IK_Joy9,        // L3
		IK_Joy10,       // R3
		IK_Joy5,        // L1
		IK_Joy6,        // R1
		IK_JoyPovUp, IK_JoyPovDown, IK_JoyPovLeft, IK_JoyPovRight,
		IK_Joy11,       // L2
		IK_Joy12,       // R2
	};

	// Triggers are buttons past half travel; the gap stops a trigger resting
	// near the threshold from chattering.
	const float TriggerPress = 0.5f;
	const float TriggerRelease = 0.35f;

	// Menus: a held d-pad or shoulder button repeats like a held key.
	const float RepeatDelay = 0.35f;
	const float RepeatRate = 0.09f;

	// Pointer speed at full deflection, in screen pixels per second, before
	// the CursorSpeed setting. Crosses the 1280-wide panel in about 1.3 s.
	const float PointerSpeed = 1000.0f;

	// Game axes in the order they are reported: left stick, right stick,
	// triggers.
	const EInputKey AxisKeys[6] = { IK_JoyX, IK_JoyY, IK_JoyU, IK_JoyV, IK_JoyZ, IK_JoyR };

	// Radial dead zone: the stick's distance from centre is rescaled so the
	// edge of the dead zone is 0 and full travel is 1, keeping its direction.
	// Per-axis dead zones make diagonals snap to the axes.
	void ApplyDeadZone(float& x, float& y, float deadZone, float curve)
	{
		float mag = std::sqrt(x * x + y * y);
		if (mag <= deadZone || mag <= 0.0f)
		{
			x = y = 0.0f;
			return;
		}
		float scaled = std::min((mag - deadZone) / (1.0f - deadZone), 1.0f);
		scaled = std::pow(scaled, curve);
		x = x / mag * scaled;
		y = y / mag * scaled;
	}

	bool IsUIOpen(Engine* engine)
	{
		// Deus Ex's interface is a stack of windows under the root; menus, the
		// inventory, conversations and keypads are modal ones.
		return engine->dxRootWindow && engine->dxRootWindow->IsModalOpen();
	}
}

void GamepadInput::Configure()
{
	if (configured)
		return;
	configured = true;
	enabled = LauncherSettings::Get().Gamepad.Enabled;
	// With our own handling on, the backend's button-to-key emulation would
	// deliver every button twice.
	DisplayBackend::Get()->SetGamepadKeyEmulation(!enabled);
}

void GamepadInput::ReadButtons(const GamepadState& state, bool down[NumButtons])
{
	for (int i = 0; i < (int)GamepadButton::Count; i++)
		down[i] = state.Buttons[i];

	const float lt = state.Axes[(int)GamepadAxis::LeftTrigger];
	const float rt = state.Axes[(int)GamepadAxis::RightTrigger];
	down[TriggerLeft] = wasDown[TriggerLeft] ? lt > TriggerRelease : lt > TriggerPress;
	down[TriggerRight] = wasDown[TriggerRight] ? rt > TriggerRelease : rt > TriggerPress;
}

bool GamepadInput::PollSkip()
{
	Configure();
	GamepadState state;
	if (!enabled || !DisplayBackend::Get()->GetGamepadState(state))
		return false;
	bool down = state.Buttons[(int)GamepadButton::A] || state.Buttons[(int)GamepadButton::B] ||
		state.Buttons[(int)GamepadButton::Back] || state.Buttons[(int)GamepadButton::Start];
	bool edge = down && !skipWasDown;
	skipWasDown = down;
	return edge;
}

void GamepadInput::Press(Engine* engine, int button, bool ui)
{
	if (!ui)
	{
		EInputKey key = GameKeys[button];
		if (key == IK_None)
			return;
		owner[button] = Owner::Game;
		engine->OnWindowKeyDown(key);
		return;
	}

	owner[button] = Owner::UI;
	const Point pos(0.0, 0.0);   // the root window clicks at its own pointer
	switch (button)
	{
	case (int)GamepadButton::A: engine->OnWindowMouseDown(pos, IK_LeftMouse); break;
	case (int)GamepadButton::X: engine->OnWindowMouseDown(pos, IK_RightMouse); break;
	// Back out of the menu. SELECT, Y and START also open menus in play, so
	// the button that opened a screen closes it again.
	case (int)GamepadButton::B:
	case (int)GamepadButton::Y:
	case (int)GamepadButton::Back:
	case (int)GamepadButton::Start: engine->OnWindowKeyDown(IK_Escape); break;
	case (int)GamepadButton::DpadUp: engine->OnWindowKeyDown(IK_Up); break;
	case (int)GamepadButton::DpadDown: engine->OnWindowKeyDown(IK_Down); break;
	case (int)GamepadButton::DpadLeft: engine->OnWindowKeyDown(IK_Left); break;
	case (int)GamepadButton::DpadRight: engine->OnWindowKeyDown(IK_Right); break;
	case (int)GamepadButton::LeftShoulder: engine->OnWindowMouseWheel(pos, IK_MouseWheelUp); break;
	case (int)GamepadButton::RightShoulder: engine->OnWindowMouseWheel(pos, IK_MouseWheelDown); break;
	default: owner[button] = Owner::None; return;
	}

	switch (button)
	{
	case (int)GamepadButton::DpadUp: case (int)GamepadButton::DpadDown:
	case (int)GamepadButton::DpadLeft: case (int)GamepadButton::DpadRight:
	case (int)GamepadButton::LeftShoulder: case (int)GamepadButton::RightShoulder:
		repeatButton = button;
		repeatWait = RepeatDelay;
		break;
	default:
		break;
	}
}

// A release goes to whichever side saw the press, even if a menu opened or
// closed in between -- otherwise a button held across the switch would stay
// down forever on one side.
void GamepadInput::Release(Engine* engine, int button)
{
	Owner was = owner[button];
	owner[button] = Owner::None;
	if (repeatButton == button)
		repeatButton = -1;

	if (was == Owner::Game)
	{
		engine->OnWindowKeyUp(GameKeys[button]);
		return;
	}
	if (was != Owner::UI)
		return;

	const Point pos(0.0, 0.0);
	switch (button)
	{
	case (int)GamepadButton::A: engine->OnWindowMouseUp(pos, IK_LeftMouse); break;
	case (int)GamepadButton::X: engine->OnWindowMouseUp(pos, IK_RightMouse); break;
	case (int)GamepadButton::B:
	case (int)GamepadButton::Y:
	case (int)GamepadButton::Back:
	case (int)GamepadButton::Start: engine->OnWindowKeyUp(IK_Escape); break;
	case (int)GamepadButton::DpadUp: engine->OnWindowKeyUp(IK_Up); break;
	case (int)GamepadButton::DpadDown: engine->OnWindowKeyUp(IK_Down); break;
	case (int)GamepadButton::DpadLeft: engine->OnWindowKeyUp(IK_Left); break;
	case (int)GamepadButton::DpadRight: engine->OnWindowKeyUp(IK_Right); break;
	default: break;   // the wheel has no release
	}
}

void GamepadInput::UIRepeat(Engine* engine, float timeElapsed)
{
	if (repeatButton < 0 || owner[repeatButton] != Owner::UI)
		return;
	repeatWait -= timeElapsed;
	if (repeatWait > 0.0f)
		return;
	repeatWait += RepeatRate;
	if (repeatWait < 0.0f)
		repeatWait = RepeatRate;   // after a long frame, one repeat, not a burst

	const Point pos(0.0, 0.0);
	switch (repeatButton)
	{
	case (int)GamepadButton::DpadUp: engine->OnWindowKeyDown(IK_Up); break;
	case (int)GamepadButton::DpadDown: engine->OnWindowKeyDown(IK_Down); break;
	case (int)GamepadButton::DpadLeft: engine->OnWindowKeyDown(IK_Left); break;
	case (int)GamepadButton::DpadRight: engine->OnWindowKeyDown(IK_Right); break;
	case (int)GamepadButton::LeftShoulder: engine->OnWindowMouseWheel(pos, IK_MouseWheelUp); break;
	case (int)GamepadButton::RightShoulder: engine->OnWindowMouseWheel(pos, IK_MouseWheelDown); break;
	default: break;
	}
}

void GamepadInput::GameAxes(Engine* engine, const GamepadState& state)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	const float deadZone = std::clamp(settings.DeadZone, 0.0f, 0.9f);

	float lx = state.Axes[(int)GamepadAxis::LeftX], ly = state.Axes[(int)GamepadAxis::LeftY];
	float rx = state.Axes[(int)GamepadAxis::RightX], ry = state.Axes[(int)GamepadAxis::RightY];
	// Movement is linear, so half a push walks at half speed. Looking gets a
	// curve: small movements for aiming, full speed at the edge.
	ApplyDeadZone(lx, ly, deadZone, 1.0f);
	ApplyDeadZone(rx, ry, deadZone, 1.6f);
	rx *= settings.LookSensitivityX;
	ry *= settings.LookSensitivityY * (settings.InvertY ? -1.0f : 1.0f);

	// SDL reports down as positive; UE1 bindings expect up to be.
	const float values[6] =
	{
		lx, -ly, rx, -ry,
		state.Axes[(int)GamepadAxis::LeftTrigger],
		state.Axes[(int)GamepadAxis::RightTrigger],
	};

	for (int i = 0; i < 6; i++)
	{
		int delta = (int)std::lround(values[i] * 100.0f);
		if (delta != 0)
		{
			// Sent every tick while deflected: the engine holds the last
			// value until the key is released.
			engine->InputEvent(AxisKeys[i], IST_Axis, delta);
			axisActive[i] = true;
		}
		else if (axisActive[i])
		{
			engine->InputEvent(AxisKeys[i], IST_Release);
			axisActive[i] = false;
		}
	}
}

void GamepadInput::ReleaseGameAxes(Engine* engine)
{
	for (int i = 0; i < 6; i++)
	{
		if (axisActive[i])
		{
			engine->InputEvent(AxisKeys[i], IST_Release);
			axisActive[i] = false;
		}
	}
}

void GamepadInput::Pointer(Engine* engine, const GamepadState& state, float timeElapsed)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	const float deadZone = std::clamp(settings.DeadZone, 0.0f, 0.9f);

	// Either stick moves the pointer; whichever is pushed further wins.
	float lx = state.Axes[(int)GamepadAxis::LeftX], ly = state.Axes[(int)GamepadAxis::LeftY];
	float rx = state.Axes[(int)GamepadAxis::RightX], ry = state.Axes[(int)GamepadAxis::RightY];
	ApplyDeadZone(lx, ly, deadZone, 2.0f);
	ApplyDeadZone(rx, ry, deadZone, 2.0f);
	float x = std::abs(rx) + std::abs(ry) > std::abs(lx) + std::abs(ly) ? rx : lx;
	float y = std::abs(rx) + std::abs(ry) > std::abs(lx) + std::abs(ly) ? ry : ly;
	if (x == 0.0f && y == 0.0f)
	{
		pointerRemX = pointerRemY = 0.0f;
		return;
	}

	float speed = PointerSpeed * std::max(settings.CursorSpeed, 0.05f) * std::min(timeElapsed, 0.1f);
	pointerRemX += x * speed;
	pointerRemY += y * speed;
	int dx = (int)pointerRemX, dy = (int)pointerRemY;
	pointerRemX -= dx;
	pointerRemY -= dy;
	if (dx != 0 || dy != 0)
		engine->OnWindowRawMouseMove(dx, dy);
}

void GamepadInput::Update(Engine* engine, float timeElapsed)
{
	Configure();
	if (!enabled)
		return;

	GamepadState state;
	if (!DisplayBackend::Get()->GetGamepadState(state))
		state = {};   // unplugged: everything reads as released

	const bool ui = IsUIOpen(engine);
	if (ui && !uiLast)
		ReleaseGameAxes(engine);   // do not keep walking under a menu
	uiLast = ui;

	bool down[NumButtons];
	ReadButtons(state, down);
	for (int i = 0; i < NumButtons; i++)
	{
		if (down[i] && !wasDown[i])
			Press(engine, i, ui);
		else if (!down[i] && wasDown[i])
			Release(engine, i);
		wasDown[i] = down[i];
	}

	if (ui)
	{
		UIRepeat(engine, timeElapsed);
		Pointer(engine, state, timeElapsed);
	}
	else
	{
		GameAxes(engine, state);
	}
}
