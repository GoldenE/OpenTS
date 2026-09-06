#include "parity.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char ** argv)
{
	try {
		asmparity::Self_Test();
		if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
			std::cout << "Harness negative control and deterministic generator passed\n";
			return 0;
		}
		if (argc == 2 && std::string_view(argv[1]) == "--negative-control") {
			std::uint8_t const actual[] = {0, 18, 255};
			std::uint8_t const expected[] = {0, 17, 255};
			asmparity::Compare("negative-control", 123, actual, expected);
			return 0;
		}
		std::filesystem::path golden;
		std::filesystem::path capture;
		if (argc == 3 && std::string_view(argv[1]) == "--golden") golden = argv[2];
		else if (argc == 3 && std::string_view(argv[1]) == "--capture") capture = argv[2];
		else if (argc != 1) throw std::runtime_error("Usage: [--golden file | --capture file | --self-test | --negative-control]");
		asmparity::Context context(OPENTS_ASM_REFERENCE != 0, golden, capture);
		Run_Suite(context);
		context.Finish();
		std::cout << "Passed " << context.Count() << " synthetic vectors\n";
		return 0;
	} catch (std::exception const & error) {
		std::cerr << "asmparity: " << error.what() << '\n';
		return 1;
	}
}
