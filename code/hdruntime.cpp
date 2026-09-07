/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "hdruntime.hh"
#include "hdruntime_state.hh"
#include "ccfile.h"
#include "cdfile.h"
#include "mixfile.h"
#include "rawfile.h"
#include "conquer.h"
#include "dbgprint.h"
#include "shapeset.h"
#include "palette.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <filesystem>
#include <type_traits>

static_assert(sizeof(ShapeSet) == 8);
static_assert(sizeof(PaletteClass) == 768);
static_assert(std::is_trivially_destructible_v<PaletteClass> && std::is_trivially_copy_constructible_v<PaletteClass>);

void ShapeSet::operator delete(void * data) noexcept
{
	HDAsset::Forget(data);
	::operator delete(data);
}

namespace HDAsset {

ScopedOwner::ScopedOwner(void const * owner) : Owner(owner)
{
	Forget(Owner);
}


ScopedOwner::~ScopedOwner()
{
	Forget(Owner);
}

namespace {

using Internal::Binding;
using Internal::State;
using Internal::Runtime;

std::string Base_Name(char const * path)
{
	std::string name = path ? path : "";
	auto slash = name.find_last_of("/\\");
	if (slash != std::string::npos) name.erase(0, slash + 1);
	return name;
}

void Diagnostic(State & state, std::string const & name, char const * reason)
{
	if (state.Diagnostics < 64) { ++state.Diagnostics; DebugString("HD asset %s: %s; using classic artwork\n", name.c_str(), reason); }
}

}

bool Track_Name(char const * name)
{
	if (!name) return false;
	auto extension = strrchr(name, '.');
	if (!extension) return false;
	for (auto known : {".SHP", ".TEM", ".SNO", ".TMP", ".PCX", ".FNT", ".PAL", ".VPL", ".VXL", ".HVA"}) if (_stricmp(extension, known) == 0) return true;
	return false;
}


void Report_Fallback(char const * name, char const * reason)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	if (state.Enabled) Diagnostic(state, name ? name : "unknown", reason ? reason : "Unsupported variant");
}


bool Enabled()
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	return state.Enabled;
}


std::uint64_t Invalidation_Generation()
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	return state.InvalidationGeneration;
}

void Configure(bool enabled, std::uint64_t cache_bytes)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	state.Enabled = enabled;
	++state.InvalidationGeneration;
	state.Assets.Set_Budget(std::clamp<std::uint64_t>(cache_bytes, 1024 * 1024, 1024ull * 1024 * 1024));
	for (auto & [pointer, binding] : state.Bindings) binding.Attempted = false;
	state.Diagnostics = 0;
}

void Register_Archive(char const * name, void const * data, int length, MixFileClass * archive)
{
	if (!data || !archive || length <= 0 || !Track_Name(name)) return;
	std::string canonical = Base_Name(name);
	if (!Normalize_Name(canonical)) return;
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	auto found = state.Bindings.find(data);
	if (found != state.Bindings.end() && found->second.Archive == archive && found->second.Origin.Name == canonical && found->second.Length == length) return;
	if (state.Bindings.size() >= 65536 && found == state.Bindings.end()) { Diagnostic(state, canonical, "Binding limit reached"); return; }
	Binding binding;
	binding.Origin = {canonical, std::string("MIX:") + archive->Filename, Policy::CACHED_MIX, ++state.Generation, {}};
	binding.Archive = archive; binding.Length = length;
	state.Bindings[data] = std::move(binding);
}

void Register_File(char const * name, void const * data, int length, MixFileClass * archive, char const * physical_path)
{
	if (!data || length <= 0 || !Track_Name(name)) return;
	std::string canonical = Base_Name(name);
	if (!Normalize_Name(canonical)) return;
	std::string identity;
	if (archive) identity = std::string("MIX:") + archive->Filename;
	else if (physical_path && *physical_path) {
		std::error_code error;
		auto path = std::filesystem::absolute(physical_path, error);
		if (error) return;
		identity = std::string("FILE:") + path.lexically_normal().string();
	} else return;
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	if (state.Bindings.size() >= 65536 && !state.Bindings.contains(data)) { Diagnostic(state, canonical, "Binding limit reached"); return; }
	Binding binding;
	binding.Origin = {canonical, identity, Policy::LOOSE_FIRST, ++state.Generation, Compute_Digest({static_cast<std::uint8_t const *>(data), static_cast<std::size_t>(length)})};
	if (binding.Origin.Content == Digest{}) { state.Bindings.erase(data); return; }
	binding.Archive = archive; binding.Length = length;
	state.Bindings[data] = std::move(binding);
}

void Invalidate_Archive(MixFileClass const * archive)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	++state.InvalidationGeneration;
	for (auto i = state.Bindings.begin(); i != state.Bindings.end();) {
		if (i->second.Archive == archive) { state.Assets.Invalidate(i->second.Origin.Identity); i = state.Bindings.erase(i); } else ++i;
	}
}

void Invalidate_All()
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	state.Bindings.clear(); state.Assets.Clear(); ++state.Generation; ++state.InvalidationGeneration;
}

void Forget(void const * data)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	state.Bindings.erase(data);
}


void Rebind(void const * data, void const * owner)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	auto found = state.Bindings.find(data);
	if (!owner || found == state.Bindings.end() || found->second.Origin.Content == Digest{}) return;
	Binding binding = std::move(found->second);
	binding.Raw = false;
	state.Bindings.erase(found);
	state.Bindings[owner] = std::move(binding);
}


std::optional<Source> Query_Source(void const * owner)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	auto found = state.Bindings.find(owner);
	if (found == state.Bindings.end()) return {};
	auto & binding = found->second;
	if (binding.Origin.Content == Digest{}) binding.Origin.Content = Compute_Digest({static_cast<std::uint8_t const *>(owner), static_cast<std::size_t>(binding.Length)});
	if (binding.Origin.Content == Digest{}) return {};
	return binding.Origin;
}


std::optional<std::size_t> Query_Source_Size(void const * owner)
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	auto found = state.Bindings.find(owner);
	if (found == state.Bindings.end() || !found->second.Raw) return {};
	return static_cast<std::size_t>(found->second.Length);
}


void Alias(void const * data, void const * owner)
{
	if (!owner || owner == data) return;
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	auto source = Query_Source(data);
	state.Bindings.erase(owner);
	if (!source || state.Bindings.size() >= 65536) return;
	auto found = state.Bindings.find(data);
	Binding binding = found->second;
	binding.Raw = false;
	state.Bindings[owner] = std::move(binding);
}


void Register_Stream(FileClass & file, void const * owner) try
{
	auto source = dynamic_cast<CCFileClass *>(&file);
	if (!source || !owner || !file.Is_Open() || !Track_Name(file.File_Name())) return;
	int position = file.Seek(0, SEEK_CUR), length = file.Size();
	if (position < 0 || length <= 0 || length > 256 * 1024 * 1024) return;
	std::vector<std::uint8_t> bytes(length);
	if (file.Seek(0, SEEK_SET) != 0) return;
	bool complete = file.Read(bytes.data(), length) == length;
	file.Seek(position, SEEK_SET);
	if (!complete) return;
	source->Register_HD_Data(bytes.data(), length);
	Rebind(bytes.data(), owner);
}
catch (std::bad_alloc const &)
{
}

Statistics Runtime_Statistics()
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	return {state.Bindings.size(), state.Assets.Bytes(), state.PackReads, state.CacheHits};
}


std::shared_ptr<Pack const> Fetch(void const * classic_data) try
{
	auto & state = Runtime();
	std::lock_guard lock(state.Mutex);
	if (!state.Enabled) return {};
	auto found = state.Bindings.find(classic_data);
	if (found == state.Bindings.end()) return {};
	auto & binding = found->second;
	if (binding.Origin.Content == Digest{}) binding.Origin.Content = Compute_Digest({static_cast<std::uint8_t const *>(classic_data), static_cast<std::size_t>(binding.Length)});
	if (auto cached = state.Assets.Find(binding.Origin)) { ++state.CacheHits; return cached; }
	if (binding.Attempted && (!binding.RetryBytes || state.Assets.Available_Bytes() < binding.RetryBytes)) return {};
	binding.Attempted = true;
	std::vector<std::uint8_t> bytes;
	int length = 0;
	std::string sidecar = binding.Origin.Name + ".HDP";
	bool read = false;
	if (binding.Archive) {
		if (binding.Archive->Read_HD_Member(sidecar.c_str(), nullptr, 0, length) && length > 0 && length <= 256 * 1024 * 1024) {
			bytes.resize(length);
			read = binding.Archive->Read_HD_Member(sidecar.c_str(), bytes.data(), length, length);
		}
	} else {
		RawFileClass file((binding.Origin.Identity.substr(5) + ".HDP").c_str());
		if (file.Is_Available() && (length = file.Size()) > 0 && length <= 256 * 1024 * 1024) {
			bytes.resize(length); read = file.Read(bytes.data(), length) == length;
		}
	}
	if (!read) { Diagnostic(state, binding.Origin.Name, "No valid sidecar in winning source"); return {}; }
	++state.PackReads;
	Pack pack;
	std::string error;
	if (!Decode(bytes, pack, error)) { Diagnostic(state, binding.Origin.Name, error.c_str()); return {}; }
	if (pack.Name != binding.Origin.Name || pack.ClassicDigest != binding.Origin.Content) { Diagnostic(state, binding.Origin.Name, "Pack does not match winning classic source"); return {}; }
	auto required = Memory_Bytes(pack);
	if (!state.Assets.Insert(binding.Origin, std::move(pack), error)) { binding.RetryBytes = required; Diagnostic(state, binding.Origin.Name, error.c_str()); return {}; }
	binding.RetryBytes = 0;
	binding.Attempted = false;
	return state.Assets.Find(binding.Origin);
}
catch (std::bad_alloc const &)
{
	return {};
}

}
