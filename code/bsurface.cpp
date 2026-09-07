/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "bsurface.h"
#include "hdruntime.hh"

BSurface::~BSurface()
{
	HDAsset::Forget(this);
}
