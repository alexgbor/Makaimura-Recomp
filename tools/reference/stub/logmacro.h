// license:BSD-3-Clause
// A stand-in for MAME's logmacro.h: the reference logs nothing
#pragma once
#define LOGMASKED(mask, ...) do { } while (false)
#define LOG(...) do { } while (false)
