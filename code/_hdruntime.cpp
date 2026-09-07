/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "hdruntime_state.hh"

HDAsset::Internal::State & HDAsset::Internal::Runtime()
{
	// Archive static destructors can call the registry after ordinary function statics die.
	static State * state = new State;
	return *state;
}
