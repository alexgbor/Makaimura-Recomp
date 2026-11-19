/**
 *    Name:        oracle_m6809.cpp
 *    Description: MAME's 6809 disassembler (mame/m6809/6x09dasm.cpp, unchanged) as an oracle: each address of
 *                 a 64 KB image, its length and text, for tools/m6809/compare_with_mame.py
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    oracle_m6809 IMAGE [FIRST LAST]   one line per address: "ADDR LENGTH TEXT" (hex address and length)
 */
#include "emu.h"
#include "6x09dasm.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: oracle_m6809 IMAGE [FIRST LAST]\n");
		return 2;
	}
	std::ifstream file(argv[1], std::ios::binary);
	std::vector<u8> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	image.resize(0x10000, 0xff);
	const unsigned first = argc > 3 ? std::stoul(argv[2], nullptr, 16) : 0;
	const unsigned last = argc > 3 ? std::stoul(argv[3], nullptr, 16) : 0xffff;

	util::disasm_interface::data_buffer buffer(image.data(), 0x10000);
	for (unsigned address = first; address <= last; address++)
	{
		m6809_disassembler disassembler;    // (a new one each time: it keeps the page it is on)
		std::ostringstream text;
		offs_t result;
		try
		{
			result = disassembler.disassemble(text, address, buffer, buffer);
		}
		catch (...)
		{
			std::printf("%04X 0 ?\n", address);
			continue;
		}
		std::printf("%04X %X %s\n", address, result & util::disasm_interface::LENGTHMASK, text.str().c_str());
	}
	return 0;
}
