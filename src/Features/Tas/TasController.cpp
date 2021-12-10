#include "TasController.hpp"

#include "Features/Tas/TasPlayer.hpp"
#include "Modules/Client.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/VGui.hpp"

#include <chrono>

Variable cl_pitchdown;
Variable cl_pitchup;

Variable sar_tas_real_controller_debug("sar_tas_real_controller_debug", "0", 0, 4, "Debugs controller.");
Variable sar_tas_playback_rate("sar_tas_playback_rate", "1.0", 0.02, "The rate at which to play back TAS scripts.");
Variable sar_tas_skipto("sar_tas_skipto", "0", 0, "Fast-forwards the TAS playback until given playback tick.");
Variable sar_tas_restore_fps("sar_tas_restore_fps", "1", 0, "Restore fps_max and host_framerate after TAS playback.");

static bool g_setPlaybackVars;

void SetPlaybackVars(bool active) {
	static bool was_active;
	static int old_forceuser;
	static int old_fpsmax;
	static int old_hostframerate;

	if (active && !was_active) {
		old_forceuser = in_forceuser.GetInt();
		old_fpsmax = fps_max.GetInt();
		old_hostframerate = host_framerate.GetInt();
		in_forceuser.SetValue(100);
		host_framerate.SetValue(60);
	} else if (!active && was_active) {
		in_forceuser.SetValue(old_forceuser);
		if (sar_tas_restore_fps.GetBool()) {
			fps_max.SetValue(old_fpsmax);
			host_framerate.SetValue(old_hostframerate);
		}
	}


	if (active) {
		if (tasPlayer->GetTick() < sar_tas_skipto.GetInt()) {
			fps_max.SetValue(0);
		} else if (tasPlayer->GetTick() >= sar_tas_skipto.GetInt()) {
			fps_max.SetValue((int)(sar_tas_playback_rate.GetFloat() * 60.0f));
		}
	}

	was_active = active;
}

TasController *tasControllers[2];

TasController::TasController() {
	for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
		buttons[i].command = g_TasControllerDigitalActions[i];
	}
	this->hasLoaded = true;

	cl_pitchdown = Variable("cl_pitchdown");
	cl_pitchup = Variable("cl_pitchup");
}

TasController::~TasController() {
	Disable();
	commandQueue.clear();
}

Vector TasController::GetMoveAnalog() {
	return moveAnalog;
}

Vector TasController::GetViewAnalog() {
	return viewAnalog;
}

void TasController::SetMoveAnalog(float x, float y) {
	moveAnalog.x = x;
	moveAnalog.y = y;
}

void TasController::SetViewAnalog(float x, float y) {
	viewAnalog.x = x;
	viewAnalog.y = y;
}

bool TasController::isEnabled() {
	return enabled;
}

void TasController::Enable() {
	enabled = true;
	SetPlaybackVars(true);
}

void TasController::Disable() {
	enabled = false;
	SetViewAnalog(0, 0);
	SetMoveAnalog(0, 0);
	for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
		buttons[i].active = true;
		buttons[i].state = false;
	}
	SetPlaybackVars(false);
	ResetDigitalInputs();
}


void TasController::ResetDigitalInputs() {
	for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
		TasControllerButton *button = &buttons[i];
		if (button->command[0] == '+') {
			char cmdbuf[128];
			snprintf(cmdbuf, sizeof(cmdbuf), "%s", button->command);
			cmdbuf[0] = '-';
			engine->ExecuteCommand(cmdbuf, true);
		}
	}
}


void TasController::AddCommandToQueue(std::string c) {
	commandQueue.push_back(c);
}

bool TasController::GetButtonState(TasControllerInput i) {
	return buttons[i].state;
}

void TasController::SetButtonState(TasControllerInput i, bool state) {
	TasControllerButton *btn = &buttons[i];
	if (btn->state != state) {
		btn->active = true;
	}
	btn->state = state;
}

std::chrono::time_point<std::chrono::high_resolution_clock> g_lastControllerMove;

void TasController::ControllerMove(int nSlot, float flFrametime, CUserCmd *cmd) {
	// ControllerMove is executed several times for one tick, idk why,
	// but only once with tick_count bigger than 0. Working only
	// on these seems to work fine, so I assume these are correct.
	if (cmd->tick_count == 0) return;

	// doing some debugs to test the behaviour of the real controller
	if (sar_tas_real_controller_debug.GetBool()) {
		int debugType = sar_tas_real_controller_debug.GetInt();
		if (debugType == 1) {
			console->Print("forwardmove: %.5f, sidemove: %.5f\n", cmd->forwardmove, cmd->sidemove);
		}

		auto now = std::chrono::high_resolution_clock::now();
		if (debugType == 2) {
			auto timePassed = std::chrono::duration_cast<std::chrono::microseconds>(now - g_lastControllerMove).count();
			console->Print("Time since last valid ControllerMove: %dus\n", timePassed);
		}

		g_lastControllerMove = now;

		if (debugType == 4) {
			console->Print("ControllerMove tick count: %d\n", cmd->tick_count);
		}
	}

	// affect inputs only if the virtual controller is enabled
	if (!enabled) return;

	//console->Print("TasController::ControllerMove (%d, ", cmd->tick_count);

	tasPlayer->FetchInputs(nSlot, this);

	//TAS is now controlling inputs. Reset everything we can.
	cmd->forwardmove = 0;
	cmd->sidemove = 0;
	cmd->upmove = 0;
	cmd->buttons = 0;

	SetPlaybackVars(true);

	//in terms of functionality, this whole stuff below is mostly a literal copy of SteamControllerMove.

	//handle digital inputs
	for (int i = 0; i < TAS_CONTROLLER_INPUT_COUNT; i++) {
		TasControllerButton *button = &buttons[i];
		if (button->active && button->command[0] == '+') {
			char cmdbuf[128];
			snprintf(cmdbuf, sizeof(cmdbuf), "%s", button->command);
			if (!button->state) {
				cmdbuf[0] = '-';
			}
			engine->ExecuteCommand(cmdbuf, true);

			//TODO: find if stuff below is needed
			/*
            IClientMode* clientMode = GetClientMode();
            if (clientMode != NULL) {
                clientMode->KeyInput(bState ? true : false, STEAMCONTROLLER_SELECT, cmdbuf);
            }
            */
		}
		button->active = false;

		if (button->state) {
			cmd->buttons |= g_TasControllerInGameButtons[i];
		}
	}

	// handle all additional commands from the command queue (not in the original, but um why not?)
	if (commandQueue.size() > 0) {
		for (std::string cmd : commandQueue) {
			char cmdbuf[128];
			snprintf(cmdbuf, sizeof(cmdbuf), "%s", cmd.c_str());
			engine->ExecuteCommand(cmdbuf, true);
		}
		commandQueue.clear();
	}

	//block analog inputs if paused (probably to block changing the view angle while paused)
	if (vgui->IsUIVisible())
		return;

	//movement analog
	if (moveAnalog.y > 0.0) {
		cmd->forwardmove += cl_forwardspeed.GetFloat() * moveAnalog.y;
	} else {
		cmd->forwardmove += cl_backspeed.GetFloat() * moveAnalog.y;
	}

	cmd->sidemove += cl_sidespeed.GetFloat() * moveAnalog.x;

	//viewangle analog

	// don't do this part if tools are enabled.
	// tools processing will do it instead
	if (!sar_tas_tools_enabled.GetBool()) {
		QAngle viewangles;
		viewangles = engine->GetAngles(GET_SLOT());

		viewangles.y -= viewAnalog.x;  // positive values should rotate right.
		viewangles.x -= viewAnalog.y;  // positive values should rotate up.
		viewangles.x = std::min(std::max(viewangles.x, -cl_pitchdown.GetFloat()), cl_pitchup.GetFloat());

		cmd->mousedx = (int)(-viewAnalog.x);
		cmd->mousedy = (int)(-viewAnalog.y);

		engine->SetAngles(GET_SLOT(), viewangles);
	}
}
