/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "hdruntime.hh"
#include "hdruntime_state.hh"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

constexpr int READ = 1;
void DebugString(char const *, ...) {}
class ShapeSet { public: std::uint8_t Header[8]; static void operator delete(void * data) noexcept; };
class PaletteClass { public: std::uint8_t Color[768]; };
class MixFileClass {
public:
	char const * Filename;
	std::map<std::string, std::vector<std::uint8_t>> Members;
	bool Read_HD_Member(char const * name, void * destination, int capacity, int & length)
	{
		auto i = Members.find(name);
		if (i == Members.end()) return false;
		length = static_cast<int>(i->second.size());
		if (!destination) return true;
		if (capacity < length) return false;
		std::memcpy(destination, i->second.data(), length);
		return true;
	}
};
class FileClass {
public:
	virtual ~FileClass() = default;
	virtual bool Is_Open() const = 0;
	virtual char const * File_Name() const = 0;
	virtual int Seek(int position, int mode) = 0;
	virtual int Size() const = 0;
	virtual int Read(void * destination, int size) = 0;
};
class CCFileClass : public FileClass {
public:
	std::vector<std::uint8_t> Data{1, 2, 3, 4};
	int Position = 0;
	bool Opened = true;
	MixFileClass * Archive = nullptr;
	std::string Name = "STREAM.HVA";
	bool Is_Open() const override { return Opened; }
	char const * File_Name() const override { return Name.c_str(); }
	int Size() const override { return static_cast<int>(Data.size()); }
	int Seek(int position, int mode) override { Position = mode == SEEK_CUR ? Position + position : position; return Position; }
	int Read(void * destination, int size) override { int count = std::min(size, Size() - Position); std::memcpy(destination, Data.data() + Position, count); Position += count; return count; }
	void Register_HD_Data(void const * bytes, int length) { HDAsset::Register_File(File_Name(), bytes, length, Archive, "STREAM.HVA"); }
};
class RawFileClass {
public:
	explicit RawFileClass(char const * name) : Name(name) {}
	bool Is_Available() const { return Files.contains(Name); }
	int Size() const { return static_cast<int>(Files.at(Name).size()); }
	int Read(void * data, int length) const
	{
		auto const & bytes = Files.at(Name);
		if (length != static_cast<int>(bytes.size())) return 0;
		std::memcpy(data, bytes.data(), bytes.size());
		return length;
	}
	static inline std::map<std::string, std::vector<std::uint8_t>> Files;
private:
	std::string Name;
};

#include "hdruntime_under_test.inc"

struct LateOwner {
	~LateOwner() { HDAsset::Forget(this); }
} Late;

void Check(bool value, char const * reason)
{
	if (!value) throw std::runtime_error(reason);
}

int main()
{
	try {
		using namespace HDAsset;
		std::vector<std::uint8_t> classic = {1, 2, 3, 4};
		Pack pack; pack.Name = "TEST.SHP"; pack.ClassicDigest = Compute_Digest(classic);
		Variant variant; variant.Scale = 2; variant.LogicalWidth = 1; variant.LogicalHeight = 1; variant.LogicalFrames = 1;
		Frame frame; frame.Width = 2; frame.Height = 2; frame.Color.resize(16, 255);
		variant.Frames.push_back(frame); pack.Variants.push_back(variant);
		std::vector<std::uint8_t> wire; std::string error;
		Check(Encode(pack, wire, error), error.c_str());
		MixFileClass base{"BASE.MIX"}, mod{"MOD.MIX"};
		base.Members["TEST.SHP.HDP"] = wire;
		Configure(false);
		Register_Archive("test.shp", classic.data(), 4, &base);
		auto token = Query_Source(classic.data()); Check(token && token->Content == pack.ClassicDigest, "Classic mode source token has raw digest");
		Register_Archive("test.shp", classic.data(), 4, &base);
		Check(Query_Source(classic.data())->Generation == token->Generation && Query_Source_Size(classic.data()) == 4, "Repeated raw registration has stable token and size");
		int parsed = 0;
		Alias(classic.data(), &parsed);
		Check(Query_Source(&parsed)->Generation == token->Generation && !Query_Source_Size(&parsed), "Parsed alias carries identity without raw span");
		Forget(&parsed); Check(!Query_Source(&parsed), "Released parsed owner loses identity");
		CCFileClass stream; stream.Archive = &base; stream.Position = 2;
		Register_Stream(stream, &parsed);
		Check(stream.Position == 2 && stream.Opened && Query_Source(&parsed)->Name == "STREAM.HVA" && !Query_Source_Size(&parsed), "Stream capture preserves cursor/open state and binds parsed owner");
		Forget(&parsed);
		for (char const * name : {"IMAGE.PCX", "FONT.FNT", "PALETTE.PAL", "VOXELS.VPL", "MOTION.HVA"}) {
			stream.Name = name;
			Register_Stream(stream, &parsed);
			Check(Query_Source(&parsed)->Name == name && !Query_Source_Size(&parsed) && stream.Position == 2, "Retained family uses parsed-owner source identity");
			Forget(&parsed);
		}
		int derived = 0;
		{
			ScopedOwner temporary(&parsed);
			Alias(classic.data(), &parsed); Alias(&parsed, &derived);
		}
		Check(!Query_Source(&parsed) && Query_Source(&derived)->Generation == token->Generation, "Derived owner provenance survives temporary palette scope");
		Forget(&derived);
		Check(!Fetch(classic.data()) && Runtime_Statistics().PackReads == 0, "Classic mode performs no pack reads");
		Configure(true);
		auto pin = Fetch(classic.data());
		Check(bool(pin), "Cached MIX sidecar found in winning archive");
		Check(Fetch(classic.data()) == pin && Runtime_Statistics().CacheHits == 1, "Repeated fetch uses cache");
		Register_Archive("TEST.SHP", classic.data(), 4, &mod);
		Check(Query_Source(classic.data())->Generation != token->Generation, "Source replacement changes immutable token");
		Check(!Fetch(classic.data()), "Mod classic override never inherits lower-priority sidecar");
		Check(pin->Name == pack.Name, "Archive rebinding preserves active draw pin");
		Invalidate_Archive(&base);
		Check(!Fetch(classic.data()), "Invalidating other archive cannot redirect winner");
		Invalidate_Archive(&mod);
		Check(Runtime_Statistics().Bindings == 0, "Archive teardown removes pointer bindings");
		auto loose = std::filesystem::absolute("WINNING/TEST.SHP").lexically_normal().string();
		RawFileClass::Files[loose + ".HDP"] = wire;
		Register_File("TEST.SHP", classic.data(), 4, nullptr, loose.c_str());
		Check(bool(Fetch(classic.data())), "Loose-file sidecar uses captured physical path");
		Forget(classic.data());
		Check(!Fetch(classic.data()), "Released owned image no longer resolves");
		classic = {9, 8, 7, 6};
		Register_File("TEST.SHP", classic.data(), 4, nullptr, loose.c_str());
		Check(!Fetch(classic.data()), "Address reuse with different classic bytes rejects stale digest");
		Invalidate_All();
		Check(Runtime_Statistics().Bindings == 0 && pin->Name == pack.Name, "Theater reset clears bindings while pins survive");
		Configure(false);
		Check(Runtime_Statistics().CacheBytes > 0, "Reconfiguration retains pinned-byte accounting");
		pin.reset(); Invalidate_All();
		Check(Runtime_Statistics().CacheBytes == 0, "Released invalidated pins reclaim budget");
		Configure(true, 1024 * 1024);
		classic = {1, 2, 3, 4};
		pack.Variants[0].Frames[0].Width = pack.Variants[0].Frames[0].Height = 384;
		pack.Variants[0].Frames[0].Color.assign(384 * 384 * 4, 255);
		Check(Encode(pack, wire, error), error.c_str());
		base.Members["TEST.SHP.HDP"] = wire; mod.Members["TEST.SHP.HDP"] = wire;
		std::array<std::uint8_t, 4> other = {1, 2, 3, 4};
		Register_Archive("TEST.SHP", classic.data(), 4, &base); Register_Archive("TEST.SHP", other.data(), 4, &mod);
		auto epoch = Invalidation_Generation();
		pin = Fetch(classic.data()); Check(bool(pin), "First large pack fits budget");
		Check(!Fetch(other.data()), "Pinned large pack prevents second allocation");
		auto reads = Runtime_Statistics().PackReads;
		Check(!Fetch(other.data()) && Runtime_Statistics().PackReads == reads, "Pinned retry does not reread pack");
		pin.reset(); Check(bool(Fetch(other.data())), "Released pin allows bounded retry");
		reads = Runtime_Statistics().PackReads;
		Check(bool(Fetch(classic.data())) && Runtime_Statistics().PackReads == reads + 1, "Successfully loaded pack is reloadable after cache eviction");
		Check(bool(Fetch(other.data())) && Runtime_Statistics().PackReads == reads + 2, "Repeated cache churn preserves both source bindings");
		Check(Invalidation_Generation() == epoch, "Registration/fetch does not invalidate decoded caches");
		Invalidate_All();
		std::vector<std::uint8_t> pointers(65535, 1);
		Register_Archive("TEST.SHP", classic.data(), 4, &base);
		for (auto & byte : pointers) Register_Archive("TEST.SHP", &byte, 1, &base);
		Check(Runtime_Statistics().Bindings == 65536, "Binding resource ceiling reached");
		mod.Members.clear(); Register_Archive("TEST.SHP", classic.data(), 4, &mod);
		Check(!Fetch(classic.data()), "Replacing an existing binding at the ceiling cannot retain a stale source");
		Invalidate_All();
		Register_Stream(stream, &Late);
		std::cout << "Production HD registry: source winner, captured loose path, digest, lifetime, cache and Classic gates passed\n";
		return 0;
	} catch (std::exception const & error) { std::cerr << error.what() << '\n'; return 1; }
}
