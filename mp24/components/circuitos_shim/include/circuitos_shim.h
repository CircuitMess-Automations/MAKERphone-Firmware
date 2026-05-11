/*
 * circuitos_shim.h — umbrella header.
 *
 * Session 2 (S-MP01) ships this as a placeholder so dependent components can
 * already `#include "circuitos_shim.h"` and the build resolves. The actual
 * API surface (Display, Input, LoopManager, Piezo) is filled in over
 * Sessions 3-6 as each subsystem comes online.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *circuitos_shim_version(void);

#ifdef __cplusplus
}
#endif
