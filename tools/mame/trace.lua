-- license:BSD-3-Clause
-- Name:        trace.lua
-- Description: MAME's own run of the game, frame by frame, for the reference and the port to be checked against:
--              at each frame (MAME's frame notifier: the screen's VBLANK) a hash of the 6809's bus 0000-2FFF (work
--              RAM, sprite RAM, the two layers), the PC, and optionally the bytes themselves and the picture
-- Version:     0.1.0
-- Created:     19-11-2026
--
-- Run by tools/mame_trace.py, which sets these environment variables:
--   MK_OUT      the output file (one line a frame: "frame hash pc")
--   MK_FRAMES   how many frames
--   MK_INPUTS   an input script (gref's format: "FRAME PORT VALUE", the byte as the board reads it, active low)
--   MK_DUMPS    a file for the bus bytes 0000-2FFF of every frame (optional)
--   MK_DSW1, MK_DSW2   the DIP switches from power-on (hex, as the board reads them: active low; optional)
--   MK_SOUND    a file for the sound board's state at every frame: "frame hash pc time", the Z80's RAM
--               (C000-C7FF)'s FNV-1a, its PC, its time in Z80 cycles (optional)
--   MK_YM       a file for every YM2203 write: "time chip offset value", time in Z80 cycles (optional)
--   MK_LATCH    a file for every write of the sound latch (3A00): "time value", time in 6809 cycles (optional)
--   MK_PICTURES, MK_PICTURES_EVERY   a file for every Nth frame's picture (screen:pixels(): 256x224 of 32-bit
--               ARGB, little endian), from frame 0 (optional)
local out_path = os.getenv("MK_OUT")
local frames = tonumber(os.getenv("MK_FRAMES") or "600")
local inputs_path = os.getenv("MK_INPUTS")
local dumps_path = os.getenv("MK_DUMPS")
local pictures_path = os.getenv("MK_PICTURES")
local pictures_every = tonumber(os.getenv("MK_PICTURES_EVERY") or "0")

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local out = assert(io.open(out_path, "w"))
local dumps = dumps_path and assert(io.open(dumps_path, "wb")) or nil
local pictures = pictures_path and assert(io.open(pictures_path, "wb")) or nil
local screen = machine.screens[":screen"]
local audiocpu = machine.devices[":audiocpu"]
local sound_path, ym_path, latch_path = os.getenv("MK_SOUND"), os.getenv("MK_YM"), os.getenv("MK_LATCH")
local sound = sound_path and assert(io.open(sound_path, "w")) or nil

-- MAME's time in a device's cycles, as its scheduler counts them (attoseconds a cycle, rounded down)
local function ticks(time, clock)
	return time.seconds * clock + time.attoseconds // (1000000000000000000 // clock)
end

-- (globals: a tap is removed once nothing refers to it)
-- MK_MARKS: the Z80's RAM at the first YM2203 write after each frame notifier, "frame write hash pc"
local marks_path = os.getenv("MK_MARKS")
mk_marks = marks_path and assert(io.open(marks_path, "w")) or nil
mk_marked, mk_marked_frame, mk_writes = false, 0, 0
if ym_path or marks_path then
	mk_ym = ym_path and assert(io.open(ym_path, "w")) or nil
	mk_ym_tap = audiocpu.spaces["program"]:install_write_tap(0xe000, 0xe003, "mk_ym", function(offset, data, mask)
		if mk_marks and mk_marked then
			local z80 = audiocpu.spaces["program"]
			local h = 2166136261
			for address = 0xc000, 0xc7ff do
				h = ((h ~ z80:read_u8(address)) * 16777619) & 0xFFFFFFFF
			end
			mk_marks:write(string.format("%d %d %08x %04x\n", mk_marked_frame, mk_writes, h, audiocpu.state["PC"].value))
			mk_marked = false
		end
		mk_writes = mk_writes + 1
		if not mk_ym then
			return
		end
		mk_ym:write(string.format("%d %d %d %02x\n", ticks(machine.time, 3000000), (offset >> 1) & 1, offset & 1, data))
	end)
end
-- MK_EVENTS: the 6809's IRQ vector fetches (FFF8) and LS259 writes, "kind time value" in 6809 cycles
local events_path = os.getenv("MK_EVENTS")
if events_path then
	mk_events = assert(io.open(events_path, "w"))
	mk_vector_tap = space:install_read_tap(0xfff8, 0xfff8, "mk_vector", function(offset, data, mask)
		mk_events:write(string.format("irq %d %02x\n", ticks(machine.time, 1500000), data))
	end)
	mk_ls259_tap = space:install_write_tap(0x3d00, 0x3d07, "mk_ls259", function(offset, data, mask)
		mk_events:write(string.format("ls259 %d %d%d\n", ticks(machine.time, 1500000), offset & 7, data & 1))
	end)
end
-- MK_Z80RAM: the Z80's first writes of its RAM, "time address value" (Z80 cycles; a check of its timeline)
local z80ram_path = os.getenv("MK_Z80RAM")
if z80ram_path then
	mk_z80ram = assert(io.open(z80ram_path, "w"))
	mk_z80ram_count = 0
	mk_z80ram_tap = audiocpu.spaces["program"]:install_write_tap(0xc000, 0xc7ff, "mk_z80ram", function(offset, data, mask)
		if mk_z80ram_count < 200 then
			mk_z80ram:write(string.format("%d %04x %02x\n", ticks(machine.time, 3000000), offset, data))
			mk_z80ram_count = mk_z80ram_count + 1
		end
	end)
end
if latch_path then
	mk_latch = assert(io.open(latch_path, "w"))
	mk_latch_tap = space:install_write_tap(0x3a00, 0x3a00, "mk_latch", function(offset, data, mask)
		mk_latch:write(string.format("%d %02x\n", ticks(machine.time, 1500000), data))
	end)
end

-- the input script: at each listed frame, a port's byte (active low) becomes the presses of its fields
local changes = {}
if inputs_path then
	for line in io.lines(inputs_path) do
		local frame, port, value = line:match("^(%d+)%s+(%a[%w]*)%s+(%x+)")
		if frame then
			table.insert(changes, { frame = tonumber(frame), port = ":" .. port:upper(), value = tonumber(value, 16) })
		end
	end
end
local next_change = 1

-- the DIP switches, set before the first frame runs: each field's setting is its bits of the byte
for _, name in ipairs({ "DSW1", "DSW2" }) do
	local value = os.getenv("MK_" .. name)
	if value then
		local byte = tonumber(value, 16)
		for _, field in pairs(machine.ioport.ports[":" .. name].fields) do
			field.user_value = byte & field.mask
		end
	end
end

local function apply(change)
	local port = machine.ioport.ports[change.port]
	if not port then
		emu.print_error("trace.lua: no port " .. change.port)
		return
	end
	if change.port ~= ":SYSTEM" and change.port ~= ":P1" and change.port ~= ":P2" then
		emu.print_error("trace.lua: only SYSTEM, P1 and P2 are pressed from a script (" .. change.port .. ")")
		return
	end
	for _, field in pairs(port.fields) do
		-- active low: a 0 bit is pressed
		local pressed = (change.value & field.mask) == 0
		field:set_value(pressed and 1 or 0)
	end
end

local function fnv(hash, byte)
	hash = hash ~ byte
	return (hash * 16777619) & 0xFFFFFFFF
end

local count = 0
-- (a global: a local subscription would be collected once the script has run, and the notifier with it)
mk_trace_subscription = emu.add_machine_frame_notifier(function()
	if count >= frames then
		return
	end
	local hash = 2166136261
	local bytes = dumps and {} or nil
	for address = 0x0000, 0x2FFF do
		local byte = space:read_u8(address)
		hash = fnv(hash, byte)
		if bytes then
			bytes[#bytes + 1] = string.char(byte)
		end
	end
	out:write(string.format("%d %08x %04x\n", count, hash, cpu.state["PC"].value))
	if dumps then
		dumps:write(table.concat(bytes))
	end
	if sound then
		local z80 = audiocpu.spaces["program"]
		local h = 2166136261
		for address = 0xc000, 0xc7ff do
			h = fnv(h, z80:read_u8(address))
		end
		sound:write(string.format("%d %08x %04x %d\n", count, h, audiocpu.state["PC"].value, ticks(machine.time, 3000000)))
	end
	if mk_marks then
		mk_marked, mk_marked_frame = true, count
	end
	-- the picture: at this notifier, screen:pixels() is the picture MAME drew at the previous VBLANK (its
	-- screen bitmap is a frame behind its frame notifier: checked, every frame of the attract mode and a game),
	-- so it is written as the previous frame's, the one gref draws at that VBLANK
	if pictures and count >= 1 and (count - 1) % pictures_every == 0 then
		pictures:write((screen:pixels()))
	end
	-- the inputs: a script's FRAME is the VBLANK after which the board reads the new value (gref applies it
	-- there). MAME reads its ports into the board's inputs in its frame update, before this notifier, so a
	-- value set now is read from the next VBLANK on: it is set one notifier early
	while next_change <= #changes and changes[next_change].frame <= count + 1 do
		apply(changes[next_change])
		next_change = next_change + 1
	end
	count = count + 1
	if count >= frames then
		out:close()
		if dumps then
			dumps:close()
		end
		if pictures then
			pictures:close()
		end
		if sound then
			sound:close()
		end
		if mk_ym then
			mk_ym:close()
		end
		if mk_latch then
			mk_latch:close()
		end
		if mk_events then
			mk_events:close()
		end
		if mk_marks then
			mk_marks:close()
		end
		if mk_z80ram then
			mk_z80ram:close()
		end
		machine:exit()
	end
end)
