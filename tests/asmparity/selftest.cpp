#include "parity.h"

void Run_Suite(asmparity::Context & context)
{
	std::uint8_t const input[] = {1, 2, 3};
	std::uint8_t const output[] = {3, 2, 1};
	context.Check("harness-roundtrip", 42, input, output, output);
}
