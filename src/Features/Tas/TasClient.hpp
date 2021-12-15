#pragma once

#include <string>

enum class PlaybackState {
	PLAYING,
	PAUSED,
	SKIPPING,
};

struct TasStatus {
	bool active;
	std::string tas_path[2];
	PlaybackState playback_state;
	float playback_rate;
	int playback_tick;
};

namespace TasClient {
	void SetStatus(TasStatus s);
};
