// SPDX-License-Identifier: MIT
#pragma once

// Default pin map for a Raspberry Pi Pico (RP2040) wired to a DB25 connector
// through 74LVC245 level shifters. Override any of these with -D if your board
// uses a different layout, but keep the data pins contiguous.
//
//   Pico GPIO            DB25 pin (direction)
//   GP0..GP7    D0..D7   2..9     Pico -> V64
//   GP8         nSTROBE  1        Pico -> V64
//   GP9         nAUTOFD  14       open drain, bidirectional
//   GP10        INIT     16       Pico -> V64
//   GP11        nSELECT  17       open drain, bidirectional
//   GP12        BUSY     11       V64 -> Pico
//   GP13        nACK     10       V64 -> Pico
//   GP14        PE       12       V64 -> Pico
//   GP15        SLCT     13       V64 -> Pico
//   GP16        nERROR   15       V64 -> Pico
//   GND         GND      18..25

#ifndef TPC2_PIN_DATA_BASE
#define TPC2_PIN_DATA_BASE 0
#endif

#ifndef TPC2_PIN_STROBE
#define TPC2_PIN_STROBE 8
#endif
#ifndef TPC2_PIN_AUTOFD
#define TPC2_PIN_AUTOFD 9
#endif
#ifndef TPC2_PIN_INIT
#define TPC2_PIN_INIT 10
#endif
#ifndef TPC2_PIN_SELECTIN
#define TPC2_PIN_SELECTIN 11
#endif

#ifndef TPC2_PIN_BUSY
#define TPC2_PIN_BUSY 12
#endif
#ifndef TPC2_PIN_ACK
#define TPC2_PIN_ACK 13
#endif
#ifndef TPC2_PIN_PE
#define TPC2_PIN_PE 14
#endif
#ifndef TPC2_PIN_SLCT
#define TPC2_PIN_SLCT 15
#endif
#ifndef TPC2_PIN_ERROR
#define TPC2_PIN_ERROR 16
#endif

// 0 means "no activity LED".
#ifndef TPC2_PIN_LED
#ifdef PICO_DEFAULT_LED_PIN
#define TPC2_PIN_LED PICO_DEFAULT_LED_PIN
#else
#define TPC2_PIN_LED 0
#endif
#endif
