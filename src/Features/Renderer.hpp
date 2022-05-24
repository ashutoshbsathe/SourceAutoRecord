#pragma once

namespace Renderer {
	void Frame();
	void Init(void **videomode);
	void Cleanup();
	extern int segmentEndTick;
	extern bool isDemoLoading;
    extern void **cached_g_videomode;
};  // namespace Renderer
