/**
 *    Name:        board.c
 *    Description: the board's bus as the 6809 sees it (gng/board.h)
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 *    After MAME's src/mame/capcom/gng.cpp (BSD-3-Clause, Pierpaolo Prazzoli): gng_main_map, bankswitch_w,
 *    machine_start, machine_reset; video/bufsprite.h (buffered_spriteram8_device::write: the copy);
 *    machine/74259.cpp (write_d0: bit 'offset' of the latch from the data's bit 0).
 *
 *    Unmapped reads give 0 (MAME's default unmap value: the driver sets no other), and so do the write-only
 *    places (palette, latch, scroll, 3C00, 3D00, 3E00). Writes to the ROM go nowhere.
 */
#include "gng/board.h"

#include <string.h>

gng_board gng;

void gng_power_on(const gng_roms *roms)
{
	memset(&gng, 0, sizeof(gng));
	if (roms)
		gng.roms = *roms;
	gng.inputs.system = 0xFF;
	gng.inputs.p1 = 0xFF;
	gng.inputs.p2 = 0xFF;
	gng.inputs.dsw1 = GNG_DSW1_DEFAULT;
	gng.inputs.dsw2 = GNG_DSW2_DEFAULT;
	/* machine_reset: "fill everything with white colors": every 4 colours 00, 55, AA, FF in both bytes */
	for (int i = 0; i < 0x100; i += 4) {
		gng.palette_base[i] = gng.palette_ext[i] = 0x00;
		gng.palette_base[i + 1] = gng.palette_ext[i + 1] = 0x55;
		gng.palette_base[i + 2] = gng.palette_ext[i + 2] = 0xAA;
		gng.palette_base[i + 3] = gng.palette_ext[i + 3] = 0xFF;
	}
}

static inline uint8_t banked(uint16_t address)
{
	const uint8_t *rom = gng.roms.maincpu;
	if (!rom)
		return 0;
	/* machine_start: entries 0-3 from 10000 in steps of 2000, entry 4 at 4000 */
	if (gng.bank == 4)
		return rom[address];
	return rom[0x10000u + gng.bank * 0x2000u + (address - 0x4000u)];
}

uint8_t gng_rom_byte(uint16_t address)
{
	if (address >= 0x6000u)
		return gng.roms.maincpu ? gng.roms.maincpu[address] : 0;
	if (address >= 0x4000u)
		return banked(address);
	return 0;
}

uint8_t gng_read(uint16_t address)
{
	if (address < 0x1E00u)
		return gng.ram[address];
	if (address < 0x2000u)
		return gng.sprite_ram[address - 0x1E00u];
	if (address < 0x2800u)
		return gng.fg_ram[address - 0x2000u];
	if (address < 0x3000u)
		return gng.bg_ram[address - 0x2800u];
	if (address < 0x4000u) {
		switch (address) {
		case 0x3000: return gng.inputs.system;
		case 0x3001: return gng.inputs.p1;
		case 0x3002: return gng.inputs.p2;
		case 0x3003: return gng.inputs.dsw1;
		case 0x3004: return gng.inputs.dsw2;
		default: return 0;
		}
	}
	return gng_rom_byte(address);
}

void gng_write(uint16_t address, uint8_t value)
{
	if (address < 0x1E00u) {
		gng.ram[address] = value;
		return;
	}
	if (address < 0x2000u) {
		gng.sprite_ram[address - 0x1E00u] = value;
		return;
	}
	if (address < 0x2800u) {
		gng.fg_ram[address - 0x2000u] = value;
		return;
	}
	if (address < 0x3000u) {
		gng.bg_ram[address - 0x2800u] = value;
		return;
	}
	if (address >= 0x3800u && address < 0x3900u) {
		gng.palette_ext[address - 0x3800u] = value;
		return;
	}
	if (address >= 0x3900u && address < 0x3A00u) {
		gng.palette_base[address - 0x3900u] = value;
		return;
	}
	switch (address) {
	case 0x3A00:
		gng.sound_latch = value;
		gng.sound_latch_writes++;
		if (gng.on_sound_latch)
			gng.on_sound_latch(&gng, value);
		return;
	case 0x3B08: case 0x3B09:
		gng.scroll_x[address - 0x3B08u] = value;
		return;
	case 0x3B0A: case 0x3B0B:
		gng.scroll_y[address - 0x3B0Au] = value;
		return;
	case 0x3C00:
		/* buffered_spriteram8_device::write: the sprite RAM copied to the video's buffer at once (the board's
		   DMA takes time and halts the 6809: MAME does not model it; see TODO.md) */
		memcpy(gng.sprite_buffer, gng.sprite_ram, sizeof(gng.sprite_buffer));
		gng.dma_count++;
		return;
	case 0x3E00:
		gng.bank = value == 4 ? 4 : (value & 3);
		return;
	default:
		break;
	}
	if (address >= 0x3D00u && address <= 0x3D07u) {
		const unsigned bit = address - 0x3D00u;
		gng.ls259 = (uint8_t)((gng.ls259 & ~(1u << bit)) | ((value & 1u) << bit));
	}
}

static uint32_t fnv(uint32_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)data;
	for (size_t i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

uint32_t gng_state_hash(void)
{
	uint32_t hash = 2166136261u;
	hash = fnv(hash, gng.ram, sizeof(gng.ram));
	hash = fnv(hash, gng.sprite_ram, sizeof(gng.sprite_ram));
	hash = fnv(hash, gng.sprite_buffer, sizeof(gng.sprite_buffer));
	hash = fnv(hash, gng.fg_ram, sizeof(gng.fg_ram));
	hash = fnv(hash, gng.bg_ram, sizeof(gng.bg_ram));
	hash = fnv(hash, gng.palette_ext, sizeof(gng.palette_ext));
	hash = fnv(hash, gng.palette_base, sizeof(gng.palette_base));
	hash = fnv(hash, gng.scroll_x, 2);
	hash = fnv(hash, gng.scroll_y, 2);
	hash = fnv(hash, &gng.ls259, 1);
	hash = fnv(hash, &gng.bank, 1);
	hash = fnv(hash, &gng.sound_latch, 1);
	return hash;
}
