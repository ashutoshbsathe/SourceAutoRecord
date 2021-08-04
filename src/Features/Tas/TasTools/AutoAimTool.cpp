#include "AutoAimTool.hpp"

#include "Modules/Console.hpp"
#include "Modules/Server.hpp"

AutoAimTool autoAimTool;

struct AutoAimParams : public TasToolParams {
	AutoAimParams()
		: TasToolParams(false) {}

	AutoAimParams(Vector point, int ticks)
		: TasToolParams(true)
		, point(point)
		, ticks(ticks)
		, elapsed(0) {}

	Vector point;
	int ticks;
	int elapsed;
};

AutoAimTool *AutoAimTool::GetTool() {
	return &autoAimTool;
}

std::shared_ptr<TasToolParams> AutoAimTool::ParseParams(std::vector<std::string> args) {
	if (args.size() == 1 && args[0] == "off") {
		return std::make_shared<AutoAimParams>();
	}

	if (args.size() == 3 || args.size() == 4) {
		float x = atof(args[0].c_str());
		float y = atof(args[1].c_str());
		float z = atof(args[2].c_str());
		int ticks = args.size() == 4 ? atoi(args[3].c_str()) : 1;

		return std::make_shared<AutoAimParams>(Vector{x, y, z}, ticks);
	}

	return nullptr;
}

void AutoAimTool::Reset() {
	this->params = std::make_shared<AutoAimParams>();
}

void AutoAimTool::Apply(TasFramebulk &bulk, const TasPlayerInfo &playerInfo) {
	auto params = std::static_pointer_cast<AutoAimParams>(this->params);
	if (!params->enabled) return;

	int remaining = 1; // If there are no lerp ticks left, pretend we're on the last tick, so that we jump all the way to the final angle
	if (params->elapsed < params->ticks) {
		remaining = params->ticks - params->elapsed;
		++params->elapsed;
	}

	void *player = server->GetPlayer(playerInfo.slot + 1);
	if (!player) return;

	Vector cam = playerInfo.position + server->GetViewOffset(player);
	Vector target = params->point;

	Vector forward = target - cam;

	float pitch = -atan2(forward.z, forward.Length2D());
	float yaw = atan2(forward.y, forward.x);

	pitch *= 180.0f / M_PI;
	yaw *= 180.0f / M_PI;

	Vector requiredDelta = QAngleToVector(playerInfo.angles) - Vector{pitch, yaw};

	while (requiredDelta.y < 0.0f) requiredDelta.y += 360.0f;
	if (requiredDelta.y > 180.0f) requiredDelta.y -= 360.0f;

	float pitchDelta = requiredDelta.x / remaining;
	float yawDelta = requiredDelta.y / remaining;

	if (sar_tas_debug.GetBool()) {
		console->Print("autoaim pitch:%.2f yaw:%.2f\n", pitch, yaw);
	}

	bulk.viewAnalog = Vector{yawDelta, pitchDelta};
}
