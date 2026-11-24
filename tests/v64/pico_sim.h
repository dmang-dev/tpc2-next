// SPDX-License-Identifier: MIT
#pragma once

#include "v64_model.h"

#ifdef __cplusplus
extern "C" {
#endif

// Connects the firmware's GPIO calls to a simulated V64 bus. Pass NULL to
// disconnect; reads then see a floating (pulled-up) cable.
void pico_sim_attach(v64_bus* bus);

#ifdef __cplusplus
}
#endif
