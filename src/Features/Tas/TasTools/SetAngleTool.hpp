#pragma once
#include "Features/Tas/TasPlayer.hpp"
#include "Features/Tas/TasTool.hpp"

class SetAngleTool : public TasTool {
public:
	SetAngleTool(int slot)
		: TasTool("setang", slot) {}
	virtual std::shared_ptr<TasToolParams> ParseParams(std::vector<std::string>);
	virtual void Apply(TasFramebulk &fb, const TasPlayerInfo &pInfo);
	virtual void Reset();
};
