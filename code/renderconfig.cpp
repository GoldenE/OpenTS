/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rendercontext.hh"
#include "ccini.h"
#include "hdruntime.hh"

void Load_Render_Settings(CCINIClass const & ini)
{
	RenderSettings settings{RenderMode::Classic, 1, 1, 1, 256u * 1024u * 1024u};
	char mode[32];
	ini.Get_String("Video", "RenderMode", "Classic", mode, sizeof(mode));
	if (_stricmp(mode, "HD") == 0) settings.Mode = RenderMode::HD;
	settings.RasterScale = ini.Get_Int("Video", "RenderScale", 2);
	settings.WorldArtScale = ini.Get_Int("Video", "WorldArtScale", settings.RasterScale);
	settings.UIArtScale = ini.Get_Int("Video", "UIArtScale", settings.RasterScale);
	int budget = ini.Get_Int("Video", "ArtCacheMB", 256);
	settings.CacheBudgetBytes = budget > 0 && budget <= 1024 ? std::size_t(budget) * 1048576 : 0;
	if (!Set_Render_Settings(settings)) Set_Render_Settings({RenderMode::Classic, 1, 1, 1, 256u * 1024u * 1024u});
	HDAsset::Configure(Get_Render_Settings().Mode == RenderMode::HD, Get_Render_Settings().CacheBudgetBytes);
}
