#pragma once

#include "Feature.hpp"
#include "Features/Hud/Hud.hpp"
#include "Utils.hpp"

#include <map>

struct Trace {
	int startSessionTick;
	int startTasTick;
	std::vector<Vector> positions[2];
	std::vector<Vector> velocities[2];
	std::vector<bool> grounded[2];
	std::vector<bool> crouched[2];
};

class PlayerTrace : public Feature {
private:
	// In order to arbitrarily number traces
	std::map<size_t, Trace> traces;

public:
	PlayerTrace();
	// Add a point to the player trace
	void AddPoint(size_t trace_idx, void *player, int slot, bool use_client_offset);
	// Returns trace with given id
	Trace* GetTrace(const size_t trace_idx);
	// Clear all the points
	void Clear(const size_t trace_idx);
	// Clear all the traces
	void ClearAll();
	// Display the trace in the world
	void DrawInWorld() const;
	// Display XY-speed delta overlay
	void DrawSpeedDeltas() const;
	// Display a bbox at the given tick
	void DrawBboxAt(int tick) const;
	// Teleport to given tick on given trace
	void TeleportAt(size_t trace, int slot, int tick);
};

extern PlayerTrace *playerTrace;

extern Vector g_playerTraceTeleportLocation;
extern int g_playerTraceTeleportSlot;
extern bool g_playerTraceNeedsTeleport;
