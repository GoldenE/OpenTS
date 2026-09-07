/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rendercontext.hh"
#include "dsurface.h"
#include "bsurface.h"
#include "abuffer.h"
#include "zbuffer.h"
#include <memory>
#include <stdexcept>

enum class StartupStep {Configuration, RenderSettings, Video, Primary, Surfaces, Game};
#include "render_startup_order.inc"

void Run_Render_Startup_Tests()
{
	auto original = Get_Render_Settings();
	for (RenderMode mode : {RenderMode::Classic, RenderMode::HD}) for (int scale = 1; scale <= 4; ++scale) {
		Set_Render_Settings({RenderMode::Classic, 1, 1, 1, 1048576});
		bool loaded = false, configured = false;
		int video_scale = 0, settings_reads = 0;
		std::unique_ptr<DSurface> primary, world;
		auto load = [&] {
			if (!configured) throw std::runtime_error("Render settings precede configuration load");
			Set_Render_Settings({mode, scale, scale, scale, 1048576});
			loaded = true; ++settings_reads;
		};
		for (auto step : Startup_Order) {
			switch (step) {
			case StartupStep::Configuration: configured = true; break;
			case StartupStep::RenderSettings: load(); break;
			case StartupStep::Video:
				if (!loaded) throw std::runtime_error("Production startup initializes video before render settings");
				video_scale = Render_Raster_Scale();
				break;
			case StartupStep::Primary: primary.reset(DSurface::Create_Primary()); break;
			case StartupStep::Surfaces: world = std::make_unique<DSurface>(16, 12, RenderDomain::World); break;
			case StartupStep::Game: if (Late_Render_Settings) load(); break;
			}
		}
		if (!primary || !world || settings_reads != 1) throw std::runtime_error("Render settings must be established once before startup allocations");
		ABuffer alpha(Rect(0, 0, 16, 12), Render_Raster_Scale());
		ZBuffer depth(Rect(0, 0, 16, 12), Render_Raster_Scale());
		int expected = mode == RenderMode::HD ? scale : 1;
		if (video_scale != expected || primary->Get_Raster_Scale() != expected || world->Get_Raster_Scale() != expected || alpha.Get_Raster_Scale() != expected || depth.Get_Raster_Scale() != expected) {
			throw std::runtime_error("New-scenario surfaces, presentation and rings disagree after production startup ordering");
		}
		auto unit = Allocate_Unit_Composite_From_Production();
		if (unit->Get_Raster_Scale() != expected || unit->Get_Render_Domain() != RenderDomain::World || unit->Get_Width() != 160 || unit->Get_Height() != 160 || unit->Stride() != 160 * expected) {
			throw std::runtime_error("Production unit composite allocator loses physical voxel density or changes logical geometry");
		}
		unit->Fill(0);
		RasterSurfaceView pixels(*unit);
		pixels.Put_Pixel(Point2D(4 * expected - 1, 4 * expected - 1), 0x42);
		if (expected > 1 && (unit->Get_Pixel(Point2D(3, 3)) != 0 || pixels.Get_Pixel(Point2D(4 * expected - 1, 4 * expected - 1)) != 0x42)) {
			throw std::runtime_error("Unit composite cannot retain independent sub-logical voxel samples");
		}
	}
	Set_Render_Settings(original);
}
