#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asmparity {

using Bytes = std::span<std::uint8_t const>;

class Random {
public:
	explicit Random(std::uint32_t seed) : State(seed) {}
	std::uint32_t Next();
	void Fill(std::span<std::uint8_t> bytes);

private:
	std::uint32_t State;
};

class Context {
public:
	Context(bool has_reference, std::filesystem::path golden = {}, std::filesystem::path capture = {});
	bool Has_Reference() const { return HasReference; }
	void Check(std::string_view name, std::uint32_t seed, Bytes input, Bytes actual, Bytes reference = {});
	void Finish();
	std::size_t Count() const { return Checks; }

private:
	struct Record {
		std::vector<std::uint8_t> Input;
		std::vector<std::uint8_t> Output;
		bool Used = false;
	};
	bool HasReference;
	bool GoldenMode;
	std::filesystem::path Capture;
	std::map<std::pair<std::string, std::uint32_t>, Record> Records;
	std::size_t Checks = 0;
};

void Compare(std::string_view name, std::uint32_t seed, Bytes actual, Bytes expected);
void Self_Test();

}

void Run_Suite(asmparity::Context & context);
