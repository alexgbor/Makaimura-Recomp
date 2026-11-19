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
		machine:exit()
	end
end)
