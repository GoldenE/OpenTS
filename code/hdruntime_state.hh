/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once
#include "hdasset.hh"
#include <map>
#include <mutex>

class MixFileClass;

namespace HDAsset::Internal {

struct Binding {
	Source Origin;
	MixFileClass * Archive = nullptr;
	int Length = 0;
	bool Attempted = false;
	bool Raw = true;
	std::uint64_t RetryBytes = 0;
};

struct State {
	std::recursive_mutex Mutex;
	std::map<void const *, Binding> Bindings;
	Cache Assets;
	std::uint64_t Generation = 1;
	std::uint64_t InvalidationGeneration = 1;
	unsigned Diagnostics = 0;
	bool Enabled = false;
	std::uint64_t PackReads = 0;
	std::uint64_t CacheHits = 0;
};

State & Runtime();

}
