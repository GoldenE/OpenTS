#include "parity.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace asmparity {
namespace {

std::string Hex(Bytes bytes)
{
	static constexpr char DIGITS[] = "0123456789abcdef";
	if (bytes.empty()) return "-";
	std::string text;
	text.reserve(bytes.size() * 2);
	for (auto byte : bytes) {
		text += DIGITS[byte >> 4];
		text += DIGITS[byte & 15];
	}
	return text;
}


std::vector<std::uint8_t> Unhex(std::string const & text)
{
	if (text == "-") return {};
	if (text.empty() || text.size() % 2 != 0) throw std::runtime_error("Invalid golden hex length");
	std::vector<std::uint8_t> result(text.size() / 2);
	for (std::size_t i = 0; i < result.size(); ++i) {
		unsigned value = 0;
		auto parsed = std::from_chars(text.data() + i * 2, text.data() + i * 2 + 2, value, 16);
		if (parsed.ec != std::errc() || parsed.ptr != text.data() + i * 2 + 2) throw std::runtime_error("Invalid golden hex byte");
		result[i] = static_cast<std::uint8_t>(value);
	}
	return result;
}

}


std::uint32_t Random::Next()
{
	State = State * 1664525u + 1013904223u;
	return State;
}


void Random::Fill(std::span<std::uint8_t> bytes)
{
	for (auto & byte : bytes) byte = static_cast<std::uint8_t>(Next() >> 24);
}


void Compare(std::string_view name, std::uint32_t seed, Bytes actual, Bytes expected)
{
	std::size_t const common = std::min(actual.size(), expected.size());
	std::size_t offset = 0;
	while (offset < common && actual[offset] == expected[offset]) ++offset;
	if (offset == common && actual.size() == expected.size()) return;
	std::ostringstream message;
	message << name << " seed=" << seed << " offset=" << offset << " actual=";
	if (offset < actual.size()) message << static_cast<unsigned>(actual[offset]); else message << "<end>";
	message << " expected=";
	if (offset < expected.size()) message << static_cast<unsigned>(expected[offset]); else message << "<end>";
	message << " sizes=" << actual.size() << '/' << expected.size();
	throw std::runtime_error(message.str());
}


Context::Context(bool has_reference, std::filesystem::path golden, std::filesystem::path capture)
	: HasReference(has_reference), GoldenMode(!golden.empty()), Capture(std::move(capture))
{
	if (!Capture.empty() && (!HasReference || GoldenMode)) throw std::runtime_error("Capture requires live Win32 assembly and no golden input");
	if (!HasReference && !GoldenMode) throw std::runtime_error("No assembly reference: supply --golden <file>");
	if (!GoldenMode) return;
	std::ifstream stream(golden);
	if (!stream) throw std::runtime_error("Cannot open golden vectors: " + golden.string());
	std::string line;
	if (!std::getline(stream, line) || line != "OPENTS_ASMPARITY_1") throw std::runtime_error("Invalid golden vector header");
	while (std::getline(stream, line)) {
		std::istringstream fields(line);
		std::string name, seed_text, input, output, extra;
		if (!(fields >> name >> seed_text >> input >> output) || fields >> extra) throw std::runtime_error("Invalid golden vector record");
		std::uint32_t seed = 0;
		auto parsed = std::from_chars(seed_text.data(), seed_text.data() + seed_text.size(), seed);
		if (parsed.ec != std::errc() || parsed.ptr != seed_text.data() + seed_text.size()) throw std::runtime_error("Invalid golden seed");
		if (!Records.emplace(std::make_pair(name, seed), Record{Unhex(input), Unhex(output)}).second) throw std::runtime_error("Duplicate golden vector: " + name);
	}
	if (stream.bad() || Records.empty()) throw std::runtime_error("Empty or unreadable golden vectors");
}


void Context::Check(std::string_view name, std::uint32_t seed, Bytes input, Bytes actual, Bytes reference)
{
	if (name.empty() || name.find_first_of(" \t\r\n") != std::string_view::npos) throw std::runtime_error("Vector names must be nonempty and contain no whitespace");
	auto key = std::make_pair(std::string(name), seed);
	if (GoldenMode) {
		auto found = Records.find(key);
		if (found == Records.end()) throw std::runtime_error("Missing golden vector: " + std::string(name) + " seed=" + std::to_string(seed));
		if (found->second.Used) throw std::runtime_error("Duplicate test vector: " + std::string(name));
		Compare(std::string(name) + "/input", seed, input, found->second.Input);
		Compare(name, seed, actual, found->second.Output);
		found->second.Used = true;
	}
	if (HasReference) Compare(std::string(name) + "/assembly", seed, actual, reference);
	if (!GoldenMode) {
		if (!Records.emplace(std::move(key), Record{{input.begin(), input.end()}, {reference.begin(), reference.end()}, true}).second) {
			throw std::runtime_error("Duplicate test vector: " + std::string(name));
		}
	}
	++Checks;
}


void Context::Finish()
{
	if (Checks == 0) throw std::runtime_error("Suite did not check any vectors");
	for (auto const & [key, record] : Records) {
		if (!record.Used) throw std::runtime_error("Unvisited golden vector: " + key.first + " seed=" + std::to_string(key.second));
	}
	if (Capture.empty()) return;
	std::ofstream stream(Capture, std::ios::trunc);
	if (!stream) throw std::runtime_error("Cannot write golden vectors: " + Capture.string());
	stream << "OPENTS_ASMPARITY_1\n";
	for (auto const & [key, record] : Records) {
		stream << key.first << '\t' << key.second << '\t' << Hex(record.Input) << '\t' << Hex(record.Output) << '\n';
	}
	stream.close();
	if (!stream) throw std::runtime_error("Failed writing golden vectors: " + Capture.string());
}


void Self_Test()
{
	std::uint8_t const original[] = {0, 17, 255};
	std::uint8_t const changed[] = {0, 18, 255};
	Compare("equal", 123, original, original);
	try {
		Compare("negative-control", 123, changed, original);
	} catch (std::runtime_error const & error) {
		if (std::string(error.what()) != "negative-control seed=123 offset=1 actual=18 expected=17 sizes=3/3") throw;
		Random random(1);
		if (random.Next() != 1015568748u || Unhex(Hex(original)) != std::vector<std::uint8_t>(std::begin(original), std::end(original))) {
			throw std::runtime_error("Generator or vector encoding self-test failed");
		}
		return;
	}
	throw std::runtime_error("Negative control did not detect the perturbed implementation");
}

}
