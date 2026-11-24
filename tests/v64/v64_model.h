// SPDX-License-Identifier: MIT
//
// Pin-level model of the Doctor V64 side of the PC parallel link.
//
// The model is plain C99 with no allocation and no I/O so it can be dropped
// into a second Pico later and act as a fake V64 for hardware-in-the-loop
// tests: feed it the electrical levels of the PC-driven DB25 pins, and drive
// the V64-owned pins from v64_model.out.
//
// Every behavioural rule carries a source tag:
//
//   [P n]   docs/PROTOCOL.md, section n
//   [D]     src/link/DemoLink.cpp
//   [T]     behaviour the original TPC 2.0 / V64IO2 host code depends on
//           (studied locally from the original sources; nothing is copied)
//   [A]     assumption: PROTOCOL.md and DemoLink are silent, so the model
//           picks the simplest behaviour that is consistent with the host
//           code. These are listed in docs/MIGRATION.md section 6.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Electrical levels (1 = high) of the pins the PC drives. AUTOFD and SELECTIN
// are open drain [P 1]: the PC either pulls them low or releases them.
typedef struct v64_host_pins {
    uint8_t data;         // D0..D7, DB25 pins 2..9
    uint8_t nstrobe;      // pin 1
    uint8_t ninit;        // pin 16
    uint8_t autofd_low;   // PC pulls nAUTOFD (pin 14) low
    uint8_t selectin_low; // PC pulls nSELECTIN (pin 17) low
} v64_host_pins;

// Electrical levels of the pins the V64 drives.
typedef struct v64_dev_pins {
    uint8_t busy;         // pin 11
    uint8_t nack;         // pin 10
    uint8_t pe;           // pin 12
    uint8_t slct;         // pin 13
    uint8_t nerror;       // pin 15
    uint8_t autofd_low;   // V64 pulls nAUTOFD low
    uint8_t selectin_low; // V64 pulls nSELECTIN low
} v64_dev_pins;

// Supported handshakes (bit mask) and the active mode.
enum {
    V64_HS_NONE = 0,
    V64_HS_OLD = 1, // SLCT_IN / AUTO.FX, BIOS before 1.2x [P 1]
    V64_HS_NEW = 2, // ERROR / SLCT, BIOS 1.2x and later [P 1]
};

enum { V64_MAX_FILES = 8, V64_OSD_MAX = 255, V64_REPLY_SILENT = -1 };

// One file announced by a GD6W download. When data is NULL the bytes are
// taken from DRAM at dram_offset, which models the V64 pushing its DRAM image.
typedef struct v64_file {
    uint8_t name[11]; // raw 8.3 field exactly as transmitted [P 3]
    const uint8_t* data;
    uint32_t dram_offset;
    uint32_t size;
} v64_file;

typedef enum v64_event_kind {
    V64_EV_SYNC,           // a = mode
    V64_EV_ABORT,          // a new sync interrupted a command (a = command)
    V64_EV_BAD_HEADER,     // header byte b rejected at offset a
    V64_EV_UPLOAD_BEGIN,   // a = size, b = counter byte, bytes = name field
    V64_EV_UPLOAD_END,     // a = bytes received
    V64_EV_DOWNLOAD_BEGIN, // a = file count, bytes = requested name field
    V64_EV_DOWNLOAD_END,   // a = response bytes sent
    V64_EV_MEM_READ,       // a = address, b = length
    V64_EV_MEM_WRITE,      // a = address, b = length (after the payload)
    V64_EV_CALL,           // a = address
    V64_EV_OSD,            // a = row byte, b = column byte, bytes = text
    V64_EV_REJECT,         // a = expanded command code, b = reply byte or -1
} v64_event_kind;

typedef struct v64_event {
    v64_event_kind kind;
    uint32_t a;
    uint32_t b;
    const uint8_t* bytes;
    size_t length;
} v64_event;

typedef void (*v64_event_fn)(void* user, const v64_event* event);

typedef struct v64_config {
    uint8_t modes;     // V64_HS_OLD | V64_HS_NEW
    bool powered;      // false: the V64 drives nothing and answers nothing
    bool extended;     // GD6E support, BIOS 1.30+ [P 4]
    bool sync_aborts;  // a sync pulse restarts the parser in any state [A]
    int reject_reply;  // byte sent instead of an ACK, or V64_REPLY_SILENT [P 4]
    int ack_override;  // -1, or a byte that replaces every GD6E ACK (tests)
    uint32_t rx_stall_after; // stop acknowledging after N bytes (0 = never)
    uint32_t tx_stall_after; // stop sending after N bytes (0 = never)

    uint8_t* dram;      // DRAM image; uploads land at offset 0 [A]
    uint32_t dram_size; // up to 32 MiB [P 4]
    uint8_t* cpu;       // 64 KiB 6502 address space, may be NULL

    v64_file files[V64_MAX_FILES];
    unsigned file_count;

    v64_event_fn on_event;
    void* user;
} v64_config;

typedef struct v64_model {
    v64_config cfg;
    v64_dev_pins out; // drive these after every v64_update()

    // --- internal state ---
    v64_host_pins prev;
    uint8_t phase;
    uint8_t sync_mode;
    uint8_t mode;      // active handshake after a successful sync
    uint8_t ack_level; // last STROBE level the V64 latched and acknowledged
    uint8_t busy;
    uint8_t nibble_valid;
    uint8_t nibble;
    uint8_t current;   // byte being sent

    uint8_t cmd;
    uint8_t hdr[20];
    uint8_t hdr_len;
    uint8_t hdr_need;
    uint32_t rx_count;
    uint32_t remaining;
    uint32_t address;
    uint32_t received;

    uint8_t tx_kind;
    uint8_t tx_stage;
    uint8_t tx_reply;
    unsigned tx_file;
    uint32_t tx_offset;
    uint32_t tx_count;
    uint8_t after_tx; // command state to enter once the reply is sent

    uint8_t osd_row;
    uint8_t osd_col;
    uint8_t osd[V64_OSD_MAX];
    size_t osd_len;
} v64_model;

// Default configuration: dual-mode BIOS 1.30+, powered, sync restarts.
void v64_config_defaults(v64_config* cfg);
void v64_init(v64_model* model, const v64_config* cfg);

// Clears all protocol state (front-panel reset) but keeps the configuration.
void v64_reset(v64_model* model);

// Call whenever any PC-driven pin changes. Updates model->out.
void v64_update(v64_model* model, const v64_host_pins* in);

// Wired-AND of the open-drain lines (1 = high).
static inline uint8_t v64_line_autofd(const v64_host_pins* h, const v64_dev_pins* d)
{
    return (uint8_t)!(h->autofd_low || d->autofd_low);
}
static inline uint8_t v64_line_selectin(const v64_host_pins* h, const v64_dev_pins* d)
{
    return (uint8_t)!(h->selectin_low || d->selectin_low);
}

// --- Host-side simulation bus (not needed on a real fake-V64 board) ---
//
// Adds a virtual microsecond clock, a configurable V64 reaction time and a
// pin-change trace. The PC register adapter (tests/v64/V64Sim.h) and the Pico
// GPIO shim both sit on top of this, so the two backends see the same V64.

enum { V64_BUS_PENDING = 32 };

typedef void (*v64_trace_fn)(void* user, uint64_t time_us, char who,
                             const v64_host_pins* host, const v64_dev_pins* dev);

typedef struct v64_bus {
    v64_model* model;
    v64_host_pins host;
    v64_dev_pins visible;
    uint64_t now_us;
    uint32_t delay_us;
    struct {
        uint64_t due;
        v64_dev_pins pins;
    } pending[V64_BUS_PENDING];
    unsigned pending_count;
    v64_trace_fn trace;
    void* trace_user;
} v64_bus;

void v64_bus_init(v64_bus* bus, v64_model* model);
void v64_bus_set_host(v64_bus* bus, const v64_host_pins* host);
void v64_bus_tick(v64_bus* bus, uint32_t us);

#ifdef __cplusplus
}
#endif
