/**
 *    Name:        ymcheck.cpp
 *    Description: the game's YM2203 (runtime/src/ym2203.c) against ymfm's (as MAME 0.289 sets it up), sample for
 *                 sample, fed the same writes at the same samples
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    ymcheck WRITES [SAMPLES]
 *      WRITES   a write log, "time chip offset value" (Z80 cycles; gref --ym-writes)
 *      SAMPLES  how many samples to compare (default: up to the last write)
 *    As the reference's sound board (tools/reference/sound/gsound.cpp): both chips reset at power-on, sample n from
 *    Z80 time 24 n, the samples that start before a write made without it. Every output (FM, SSG A, B, C) of both
 *    chips compared; the first differences are printed.
 */
#include "ymfm_opn.h"

extern "C" {
#include "gng/ym2203.h"
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
class reference_chip : public ymfm::ymfm_interface
{
public:
	reference_chip() : m_chip(*this) { m_chip.set_fidelity(ymfm::OPN_FIDELITY_MED); }
	void reset() { m_chip.reset(); }
	void write(uint32_t offset, uint8_t data) { m_chip.write(offset, data); }
	void generate(int32_t out[4])
	{
		ymfm::ym2203::output_data output;
		m_chip.generate(&output, 1);
		for (int i = 0; i < 4; i++)
			out[i] = output.data[i];
	}

private:
	ymfm::ym2203 m_chip;
};

struct write_entry { unsigned long long time; int chip, offset; unsigned value; };
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "ymcheck WRITES [SAMPLES]\n");
		return 2;
	}
	std::vector<write_entry> writes;
	FILE *file = std::fopen(argv[1], "r");
	if (!file)
		return 2;
	write_entry w;
	while (std::fscanf(file, "%llu %d %d %x", &w.time, &w.chip, &w.offset, &w.value) == 4)
		writes.push_back(w);
	std::fclose(file);
	const unsigned long long samples = argc > 2 ? std::strtoull(argv[2], nullptr, 10) :
		(writes.empty() ? 0 : writes.back().time / 24 + 1);

	static reference_chip reference[2];
	static ym2203 ours[2];
	for (int i = 0; i < 2; i++)
	{
		reference[i].reset();
		ym2203_init(&ours[i]);
		ym2203_reset(&ours[i]);
	}
	size_t next = 0;
	unsigned long long differences = 0;
	for (unsigned long long n = 0; n < samples; n++)
	{
		// the writes made before this sample starts
		while (next < writes.size() && writes[next].time <= 24 * n)
		{
			const write_entry &e = writes[next++];
			reference[e.chip].write(unsigned(e.offset), uint8_t(e.value));
			ym2203_write(&ours[e.chip], unsigned(e.offset), uint8_t(e.value));
		}
		for (int chip = 0; chip < 2; chip++)
		{
			int32_t a[4], b[4];
			reference[chip].generate(a);
			ym2203_generate(&ours[chip], b);
			for (int i = 0; i < 4; i++)
				if (a[i] != b[i])
				{
					if (differences < 10)
						std::printf("sample %llu chip %d output %d: ymfm %d, ours %d\n", n, chip, i, a[i], b[i]);
					differences++;
				}
		}
	}
	std::printf("%llu samples of 2 chips (%zu writes): %llu outputs different\n", samples, next, differences);
	return differences ? 1 : 0;
}
