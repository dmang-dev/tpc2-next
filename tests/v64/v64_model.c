// SPDX-License-Identifier: MIT
//
// Doctor V64 pin model. See v64_model.h for the source tags used below.

#include "v64_model.h"

#include <string.h>

enum {
    PH_WAIT_SYNC, // not listening for bytes; only a sync pulse is accepted
    PH_SYNC_A,    // first sync pattern acknowledged, waiting for INIT high
    PH_SYNC_B,    // waiting for the second sync pattern
    PH_SYNC_C,    // second pattern acknowledged, waiting for INIT high
    PH_RX,        // receiving bytes on the STROBE handshake
    PH_TX_ARM,    // reply pending, waiting for the PC to drop D7
    PH_TX_HIGH,   // BUSY high: waiting for D7 high (next low nibble)
    PH_TX_LOW,    // low nibble shown: waiting for D7 low (high nibble)
};

enum {
    CMD_NONE,
    CMD_HEADER,
    CMD_UPLOAD,
    CMD_MEMWRITE,
    CMD_OSD,
};

enum {
    TX_SINGLE,   // one reply byte (ACK or reject)
    TX_MEMREAD,  // ACK followed by memory contents
    TX_DOWNLOAD, // file count, then header + data per file
};

static void emit(v64_model* m, v64_event_kind kind, uint32_t a, uint32_t b,
                 const uint8_t* bytes, size_t length)
{
    if (!m->cfg.on_event)
        return;
    v64_event ev;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    ev.bytes = bytes;
    ev.length = length;
    m->cfg.on_event(m->cfg.user, &ev);
}

void v64_config_defaults(v64_config* cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->modes = V64_HS_OLD | V64_HS_NEW;
    cfg->powered = true;
    cfg->extended = true;
    cfg->sync_aborts = true;
    // [P 4] says the V64 "reports a non-ACK byte"; the value is not
    // documented anywhere, so 0x00 is an arbitrary choice [A].
    cfg->reject_reply = 0x00;
    cfg->ack_override = -1;
}

static void drive(v64_model* m)
{
    v64_dev_pins* o = &m->out;
    if (!m->cfg.powered) {
        // [A] An unpowered V64 drives every line low and pulls nothing.
        memset(o, 0, sizeof *o);
        return;
    }

    o->busy = m->busy;
    if (m->nibble_valid) {
        // [P 1 "Reading bytes"] The nibble is read from status bits 3..6.
        // A real PC does not invert those bits, so the pin levels equal the
        // nibble bits: nERROR = b0, SLCT = b1, PE = b2, nACK = b3.
        o->nerror = (uint8_t)(m->nibble & 1u);
        o->slct = (uint8_t)((m->nibble >> 1) & 1u);
        o->pe = (uint8_t)((m->nibble >> 2) & 1u);
        o->nack = (uint8_t)((m->nibble >> 3) & 1u);
    } else {
        const bool pulse = m->phase == PH_SYNC_A || m->phase == PH_SYNC_C;
        // [P 1 "new handshake"] ERROR idles with status bit 3 set and is
        // pulled low to acknowledge each sync pattern.
        o->nerror = (uint8_t)!(pulse && m->sync_mode == V64_HS_NEW);
        // [P 1 "Writing bytes"] New mode: SLCT follows the last STROBE level
        // the V64 latched (even byte waits SLCT=1, odd byte waits SLCT=0).
        o->slct = m->mode == V64_HS_NEW ? m->ack_level : 1u;
        o->pe = 0;
        o->nack = 1;
    }
    // [P 1 "old handshake"] SELECTIN is pulled low to acknowledge a pattern.
    o->selectin_low = (uint8_t)((m->phase == PH_SYNC_A || m->phase == PH_SYNC_C)
                                && m->sync_mode == V64_HS_OLD);
    // [P 1 "Writing bytes"] Old mode: AUTOFD is pulled low after a STROBE-low
    // byte and released after a STROBE-high byte (even waits AUTO.FX=0 in
    // the control register, odd waits AUTO.FX=1).
    o->autofd_low = (uint8_t)(m->mode == V64_HS_OLD && m->ack_level == 0);
}

void v64_reset(v64_model* m)
{
    const v64_config cfg = m->cfg;
    memset(m, 0, sizeof *m);
    m->cfg = cfg;
    m->phase = PH_WAIT_SYNC;
    m->ack_level = 1;
    m->prev.nstrobe = 1;
    m->prev.ninit = 1;
    // [A] BUSY is held low while the V64 is not sending; checkSync waits for
    // the V64 to raise it ([P 1 "Command acknowledgement"], status bit 7 = 0).
    m->busy = 0;
    drive(m);
}

void v64_init(v64_model* m, const v64_config* cfg)
{
    memset(m, 0, sizeof *m);
    m->cfg = *cfg;
    v64_reset(m);
}

static void stop_listening(v64_model* m)
{
    m->phase = PH_WAIT_SYNC;
    m->cmd = CMD_NONE;
    m->nibble_valid = 0;
    m->busy = 0;
}

// --- memory map [P 4 "Address spaces"] -------------------------------------

static uint8_t* map_memory(v64_model* m, uint32_t address, uint32_t length,
                           bool write)
{
    if (length == 0)
        return NULL; // [A] the "length-1" comment in V64IO2 is not followed [P 4]
    if (address & 0x80000000u) {
        // CPU space. [D] only accepts 0x8000xxxx; the middle bits are
        // otherwise undocumented [A].
        if (!m->cfg.cpu || (address & 0x7FFF0000u) != 0)
            return NULL;
        const uint32_t lo = address & 0xFFFFu;
        const uint64_t end = (uint64_t)lo + length;
        if (write) {
            if (lo < 0x0200u || end > 0x0800u) // [P 4] CPU write window
                return NULL;
        } else {
            if (end > 0x10000u) // [P 4] CPU read: 0x0000-0xFFFF ...
                return NULL;
            if (lo < 0xD000u && end > 0xC000u) // ... except 0xC000-0xCFFF
                return NULL;
        }
        return m->cfg.cpu + lo;
    }
    // DRAM: top bit clear, at most 32 MiB [P 4]; bounded by the model's buffer.
    if ((uint64_t)address + length > m->cfg.dram_size
        || (uint64_t)address + length > 0x02000000ull || !m->cfg.dram)
        return NULL;
    return m->cfg.dram + address;
}

// --- reply generator --------------------------------------------------------

static const uint8_t* file_bytes(const v64_model* m, const v64_file* f)
{
    if (f->data)
        return f->data;
    if (!m->cfg.dram || (uint64_t)f->dram_offset + f->size > m->cfg.dram_size)
        return NULL;
    return m->cfg.dram + f->dram_offset;
}

static bool tx_next(v64_model* m, uint8_t* out)
{
    switch (m->tx_kind) {
    case TX_SINGLE:
        if (m->tx_stage == 0) {
            m->tx_stage = 1;
            *out = m->tx_reply;
            return true;
        }
        return false;

    case TX_MEMREAD:
        if (m->tx_stage == 0) {
            m->tx_stage = 1;
            *out = m->tx_reply;
            return true;
        }
        if (m->tx_offset < m->remaining) {
            const uint8_t* p = map_memory(m, m->address, m->remaining, false);
            *out = p ? p[m->tx_offset] : 0;
            ++m->tx_offset;
            return true;
        }
        return false;

    case TX_DOWNLOAD:
        // [P 3] count byte, then per file: size_le32 + name[11] + data.
        for (;;) {
            if (m->tx_stage == 0) {
                m->tx_stage = 1;
                m->tx_file = 0;
                m->tx_offset = 0;
                *out = (uint8_t)m->cfg.file_count;
                return true;
            }
            if (m->tx_file >= m->cfg.file_count)
                return false;
            const v64_file* f = &m->cfg.files[m->tx_file];
            if (m->tx_stage == 1) {
                if (m->tx_offset < 4) {
                    *out = (uint8_t)(f->size >> (8u * m->tx_offset));
                    ++m->tx_offset;
                    return true;
                }
                if (m->tx_offset < 15) {
                    *out = f->name[m->tx_offset - 4];
                    ++m->tx_offset;
                    return true;
                }
                m->tx_stage = 2;
                m->tx_offset = 0;
            }
            if (m->tx_offset < f->size) {
                const uint8_t* p = file_bytes(m, f);
                *out = p ? p[m->tx_offset] : 0;
                ++m->tx_offset;
                return true;
            }
            ++m->tx_file;
            m->tx_stage = 1;
            m->tx_offset = 0;
        }
    default:
        return false;
    }
}

static void start_reply(v64_model* m, uint8_t kind, uint8_t reply, uint8_t after)
{
    m->tx_kind = kind;
    m->tx_stage = 0;
    m->tx_reply = reply;
    m->tx_offset = 0;
    m->tx_count = 0;
    m->after_tx = after;
    // [P 1 "Command acknowledgement"] After a header the V64 waits for the
    // PC to drop D7, then raises BUSY (status bit 7 reads 0).
    m->phase = PH_TX_ARM;
}

static uint8_t ack_byte(const v64_model* m, uint8_t ack)
{
    return m->cfg.ack_override >= 0 ? (uint8_t)m->cfg.ack_override : ack;
}

static void reject(v64_model* m, uint8_t code)
{
    emit(m, V64_EV_REJECT, code, (uint32_t)m->cfg.reject_reply, NULL, 0);
    if (m->cfg.reject_reply == V64_REPLY_SILENT) {
        stop_listening(m); // [A] never raises BUSY; checkSync times out
        return;
    }
    start_reply(m, TX_SINGLE, (uint8_t)m->cfg.reject_reply, CMD_NONE);
}

static void bad_header(v64_model* m, uint8_t value)
{
    // [A] An unknown header (including GD6E on a BIOS older than 1.30) makes
    // the V64 stop acknowledging bytes; the PC's next write times out.
    // [D] throws a host-side Protocol error instead.
    emit(m, V64_EV_BAD_HEADER, m->hdr_len, value, m->hdr, m->hdr_len);
    stop_listening(m);
}

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void finish_command(v64_model* m)
{
    // [A] After a command the BIOS goes back to waiting for a sync; every
    // host tool syncs before each command [P 2-4].
    stop_listening(m);
}

static void execute_header(v64_model* m)
{
    const uint8_t command = m->hdr[3];
    if (command == 'R') {
        // [P 2] "GD6R" counter size_le32 name[11]; the counter is ignored.
        m->remaining = le32(m->hdr + 5);
        m->received = 0;
        emit(m, V64_EV_UPLOAD_BEGIN, m->remaining, m->hdr[4], m->hdr + 9, 11);
        m->cmd = CMD_UPLOAD;
        if (m->remaining == 0) {
            emit(m, V64_EV_UPLOAD_END, 0, 0, m->hdr + 9, 11);
            finish_command(m);
        }
        return;
    }
    if (command == 'W') {
        // [P 3] "GD6W" name[11], then the V64 answers after checkSync.
        emit(m, V64_EV_DOWNLOAD_BEGIN, m->cfg.file_count, 0, m->hdr + 4, 11);
        m->cmd = CMD_NONE;
        start_reply(m, TX_DOWNLOAD, 0, CMD_NONE);
        return;
    }

    // 'E' extended commands [P 4]; arguments are big-endian.
    const uint8_t code = m->hdr[4];
    switch (code) {
    case 'R': {
        const uint32_t address = be32(m->hdr + 5);
        const uint32_t length = be32(m->hdr + 9);
        if (!map_memory(m, address, length, false)) {
            reject(m, code);
            return;
        }
        emit(m, V64_EV_MEM_READ, address, length, NULL, 0);
        m->address = address;
        m->remaining = length;
        start_reply(m, TX_MEMREAD, ack_byte(m, 0xA0), CMD_NONE);
        return;
    }
    case 'W': {
        const uint32_t address = be32(m->hdr + 5);
        const uint32_t length = be32(m->hdr + 9);
        if (!map_memory(m, address, length, true)) {
            reject(m, code);
            return;
        }
        m->address = address;
        m->remaining = length;
        m->received = 0;
        start_reply(m, TX_SINGLE, ack_byte(m, 0xA1), CMD_MEMWRITE);
        return;
    }
    case 'C': {
        // [P 4] address_be16, ACK 0xA2, no payload.
        const uint32_t address = ((uint32_t)m->hdr[5] << 8) | m->hdr[6];
        emit(m, V64_EV_CALL, address, 0, NULL, 0);
        start_reply(m, TX_SINGLE, ack_byte(m, 0xA2), CMD_NONE);
        return;
    }
    case 'P': {
        // [P 4] row 0-10, column 0-23. V64IO2 masks the row with 0xEF and its
        // help text puts the flash flag in row bit 7, so only the low bits are
        // range-checked here [A]; [D] rejects row > 10 outright.
        const uint8_t row = m->hdr[5];
        const uint8_t col = m->hdr[6];
        if ((row & 0x0Fu) > 0x0Au || (col & 0x1Fu) > 0x17u) {
            reject(m, code);
            return;
        }
        m->osd_row = row;
        m->osd_col = col;
        m->osd_len = 0;
        start_reply(m, TX_SINGLE, ack_byte(m, 0xA3), CMD_OSD);
        return;
    }
    default:
        bad_header(m, code);
        return;
    }
}

static void receive(v64_model* m, uint8_t value)
{
    switch (m->cmd) {
    case CMD_HEADER: {
        static const uint8_t magic[3] = { 'G', 'D', '6' };
        m->hdr[m->hdr_len++] = value;
        if (m->hdr_len <= 3) {
            if (value != magic[m->hdr_len - 1])
                bad_header(m, value);
            return;
        }
        if (m->hdr_len == 4) {
            if (value == 'R')
                m->hdr_need = 20; // [P 2] 5 + 4 + 11
            else if (value == 'W')
                m->hdr_need = 15; // [P 3] 4 + 11
            else if (value == 'E' && m->cfg.extended)
                m->hdr_need = 5;  // [P 4] code follows
            else {
                bad_header(m, value);
                return;
            }
        } else if (m->hdr_len == 5 && m->hdr[3] == 'E') {
            if (value == 'R' || value == 'W')
                m->hdr_need = 13; // [P 4] address_be32 + length_be32
            else if (value == 'C' || value == 'P')
                m->hdr_need = 7;  // [P 4] two argument bytes
            else {
                bad_header(m, value);
                return;
            }
        }
        if (m->hdr_len == m->hdr_need)
            execute_header(m);
        return;
    }

    case CMD_UPLOAD:
        // [A] The image is stored in DRAM from offset 0. Bytes past the end
        // of the model's buffer are acknowledged and discarded.
        if (m->cfg.dram && m->received < m->cfg.dram_size)
            m->cfg.dram[m->received] = value;
        ++m->received;
        if (--m->remaining == 0) {
            emit(m, V64_EV_UPLOAD_END, m->received, 0, m->hdr + 9, 11);
            finish_command(m);
        }
        return;

    case CMD_MEMWRITE: {
        uint8_t* p = map_memory(m, m->address, m->remaining, true);
        if (p)
            *p = value;
        ++m->address;
        ++m->received;
        if (--m->remaining == 0) {
            emit(m, V64_EV_MEM_WRITE, m->address - m->received, m->received,
                 NULL, 0);
            finish_command(m);
        }
        return;
    }

    case CMD_OSD:
        // [P 4 "OSD text"] characters up to a 0xFF terminator.
        if (value == 0xFF) {
            emit(m, V64_EV_OSD, m->osd_row, m->osd_col, m->osd, m->osd_len);
            finish_command(m);
            return;
        }
        if (m->osd_len < V64_OSD_MAX)
            m->osd[m->osd_len++] = value;
        return;

    default:
        return;
    }
}

static bool command_in_progress(const v64_model* m)
{
    if (m->phase >= PH_TX_ARM)
        return true;
    return m->phase == PH_RX && (m->cmd != CMD_HEADER || m->hdr_len > 0);
}

static void handle_init(v64_model* m, const v64_host_pins* in)
{
    const bool fell = m->prev.ninit && !in->ninit;
    const bool rose = !m->prev.ninit && in->ninit;

    if (fell) {
        const bool old_start = in->data == 0xAA && (m->cfg.modes & V64_HS_OLD);
        const bool new_start = in->data == 0xA5 && (m->cfg.modes & V64_HS_NEW);
        if (m->phase == PH_SYNC_B
            && ((m->sync_mode == V64_HS_OLD && in->data == 0x55)
                || (m->sync_mode == V64_HS_NEW && in->data == 0x5A))) {
            m->phase = PH_SYNC_C; // [P 1] second pattern
        } else if (old_start || new_start) {
            if (command_in_progress(m)) {
                if (!m->cfg.sync_aborts)
                    return; // [A] a strict BIOS would ignore INIT mid-command
                emit(m, V64_EV_ABORT, m->hdr_len >= 4 ? m->hdr[3] : 0, 0, NULL, 0);
            }
            // [P 1] first pattern: 0xAA (old) or 0xA5 (new) with INIT low.
            m->sync_mode = old_start ? V64_HS_OLD : V64_HS_NEW;
            m->mode = V64_HS_NONE;
            m->nibble_valid = 0;
            m->busy = 0;
            m->cmd = CMD_NONE;
            m->phase = PH_SYNC_A;
        } else if (m->phase == PH_SYNC_A || m->phase == PH_SYNC_B
                   || m->phase == PH_SYNC_C) {
            m->phase = PH_WAIT_SYNC; // wrong pattern: give up this attempt
        }
    } else if (rose) {
        if (m->phase == PH_SYNC_A) {
            m->phase = PH_SYNC_B;
        } else if (m->phase == PH_SYNC_C) {
            // [P 1] Synchronised. The byte phase restarts from the current
            // STROBE level, which the host leaves high (control 0x04).
            m->mode = m->sync_mode;
            m->ack_level = in->nstrobe;
            m->phase = PH_RX;
            m->cmd = CMD_HEADER;
            m->hdr_len = 0;
            m->hdr_need = 4;
            m->rx_count = 0;
            emit(m, V64_EV_SYNC, m->mode, 0, NULL, 0);
        }
    }
}

static void leave_tx(v64_model* m)
{
    m->nibble_valid = 0;
    m->busy = 0;
    if (m->tx_kind == TX_DOWNLOAD)
        emit(m, V64_EV_DOWNLOAD_END, m->tx_count, 0, NULL, 0);
    if (m->after_tx == CMD_NONE) {
        finish_command(m);
        return;
    }
    m->cmd = m->after_tx;
    m->phase = PH_RX; // payload follows on the STROBE handshake [P 4]
}

void v64_update(v64_model* m, const v64_host_pins* in)
{
    if (!m->cfg.powered) {
        m->prev = *in;
        drive(m);
        return;
    }

    handle_init(m, in);

    if (m->phase == PH_RX && in->nstrobe != m->ack_level) {
        // [P 1 "Writing bytes"] The V64 latches on every STROBE edge and
        // acknowledges by moving AUTOFD (old) or SLCT (new) to match.
        const bool stalled = m->cfg.rx_stall_after
            && m->rx_count >= m->cfg.rx_stall_after;
        if (!stalled) {
            m->ack_level = in->nstrobe;
            ++m->rx_count;
            receive(m, in->data);
        }
    }

    const uint8_t d7 = (uint8_t)(in->data >> 7);
    if (m->phase == PH_TX_ARM && !d7) {
        m->busy = 1; // status bit 7 reads 0
        m->phase = PH_TX_HIGH;
    } else if (m->phase == PH_TX_HIGH && d7) {
        // [P 1 "Reading bytes"] D7 high requests the low nibble; the same
        // edge acknowledges the previous high nibble.
        const bool stalled = m->cfg.tx_stall_after
            && m->tx_count >= m->cfg.tx_stall_after;
        uint8_t value = 0;
        if (stalled) {
            // keep BUSY high: the PC times out waiting for the low nibble
        } else if (tx_next(m, &value)) {
            m->current = value;
            m->nibble = (uint8_t)(value & 0x0Fu);
            m->nibble_valid = 1;
            m->busy = 0; // status bit 7 reads 1
            m->phase = PH_TX_LOW;
        } else {
            leave_tx(m);
        }
    } else if (m->phase == PH_TX_LOW && !d7) {
        m->nibble = (uint8_t)(m->current >> 4);
        m->busy = 1;
        m->phase = PH_TX_HIGH;
        ++m->tx_count;
    }

    m->prev = *in;
    drive(m);
}

// --- simulation bus ---------------------------------------------------------

static bool same_dev(const v64_dev_pins* a, const v64_dev_pins* b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

static void publish(v64_bus* bus, const v64_dev_pins* pins)
{
    if (same_dev(&bus->visible, pins))
        return;
    bus->visible = *pins;
    if (bus->trace)
        bus->trace(bus->trace_user, bus->now_us, 'V', &bus->host, &bus->visible);
}

static void apply_due(v64_bus* bus)
{
    unsigned keep = 0;
    for (unsigned i = 0; i < bus->pending_count; ++i) {
        if (bus->pending[i].due <= bus->now_us)
            publish(bus, &bus->pending[i].pins);
        else
            bus->pending[keep++] = bus->pending[i];
    }
    bus->pending_count = keep;
}

void v64_bus_init(v64_bus* bus, v64_model* model)
{
    memset(bus, 0, sizeof *bus);
    bus->model = model;
    bus->host.nstrobe = 1;
    bus->host.ninit = 1;
    bus->visible = model->out;
}

void v64_bus_set_host(v64_bus* bus, const v64_host_pins* host)
{
    if (memcmp(&bus->host, host, sizeof *host) == 0)
        return;
    bus->host = *host;
    if (bus->trace)
        bus->trace(bus->trace_user, bus->now_us, 'H', &bus->host, &bus->visible);
    v64_update(bus->model, host);
    if (bus->delay_us == 0) {
        publish(bus, &bus->model->out);
        return;
    }
    if (bus->pending_count == V64_BUS_PENDING) {
        // Queue full: deliver everything now rather than drop a change.
        for (unsigned i = 0; i < bus->pending_count; ++i)
            publish(bus, &bus->pending[i].pins);
        bus->pending_count = 0;
    }
    bus->pending[bus->pending_count].due = bus->now_us + bus->delay_us;
    bus->pending[bus->pending_count].pins = bus->model->out;
    ++bus->pending_count;
}

void v64_bus_tick(v64_bus* bus, uint32_t us)
{
    bus->now_us += us;
    if (bus->pending_count)
        apply_due(bus);
}
