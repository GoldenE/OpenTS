/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once

#include "hdasset.hh"
#include <optional>

class MixFileClass;
class FileClass;

namespace HDAsset {

struct Statistics {
	std::uint64_t Bindings = 0;
	std::uint64_t CacheBytes = 0;
	std::uint64_t PackReads = 0;
	std::uint64_t CacheHits = 0;
};

class ScopedOwner {
public:
	explicit ScopedOwner(void const * owner);
	~ScopedOwner();
	ScopedOwner(ScopedOwner const &) = delete;
	ScopedOwner & operator=(ScopedOwner const &) = delete;
private:
	void const * Owner;
};

void Configure(bool enabled, std::uint64_t cache_bytes = 256u * 1024u * 1024u);
bool Enabled();
std::uint64_t Invalidation_Generation();
bool Track_Name(char const * name);
void Register_Archive(char const * name, void const * data, int length, MixFileClass * archive);
void Register_File(char const * name, void const * data, int length, MixFileClass * archive, char const * physical_path);
void Invalidate_Archive(MixFileClass const * archive);
void Invalidate_All();
void Forget(void const * data);
void Rebind(void const * data, void const * owner);
std::optional<Source> Query_Source(void const * owner);
std::optional<std::size_t> Query_Source_Size(void const * owner);
void Alias(void const * data, void const * owner);
void Register_Stream(FileClass & file, void const * owner);
std::shared_ptr<Pack const> Fetch(void const * classic_data);
Statistics Runtime_Statistics();
void Report_Fallback(char const * name, char const * reason);

}
