/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

void Sim_Tick_Advance(void);
void Sim_Tick_End(void);
void Render_Frame_Begin(void);
void Report_Render_Offsets(void);

/// <summary>Returns the alpha sampled for this frame, from 0 through 256 inclusive.</summary>
int Fetch_Render_Alpha(void);
