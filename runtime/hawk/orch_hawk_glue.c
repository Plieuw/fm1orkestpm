/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * HAWK/Felucca side of the orchestral firmware: included into felucca.c by
 * tools/prepare_hawk_runtime.py, after the storage layer and before ui.c, so
 * it sees HAWK's static hardware layer (fm1_in, the LCD, the LED dim map, the
 * MIDI rings, the A/B storage). It does three things:
 *
 *  - orch_hawk_keys_block(): takes the place of HAWK's keys_block() in the
 *    audio interrupt. The 27 note keys go to the orchestra's selected part
 *    (through the panel's transpose and scale), USB and TRS MIDI in go to the
 *    engine on their own channels (one part a channel, 1..8), the realtime
 *    bytes to its ARP/phrase clock. HAWK's synth, ARP and KARMA are not run.
 *  - orch_hw_*(): the handful of functions the panel needs (orch_hawk_hw.h).
 *  - the second core: handed to the engine (orch_set_second_core), which
 *    renders half the sounding voices on it.
 *  - orch_hawk_main_frame(): the panel's turn in the main loop, in place of
 *    ui_input() / ui_leds() / ui_draw(). While OCT- and OCT+ are both held the
 *    UBOOT countdown owns the screen, as in HAWK; when they are let go, the
 *    panel paints itself whole again.
 *
 * Everything HAWK keeps — the watchdog, the boot-loop guard, OCT-+OCT+ for 5 s
 * into UBOOT, the USB SysEx update entry, M-UPGRADE, the remote screen — stays
 * in main.c, untouched.
 */
#include "orchestra/orch_hawk_bridge.h"
#include "orchestra/orch_hawk_hw.h"

#define ORCH_KEY_VELOCITY 100u   /* the keys have no velocity (as HAWK's KEY_VEL) */
#define ORCH_STORE_OBJ (OBJ_USER0 + 0u)

/* --- the audio interrupt -------------------------------------------------- */

static uint32_t orch_kb_prev;

static void orch_hawk_keys_block(void)
{
    if (keys_panic_req) {
        keys_panic_req = 0;
        orch_hawk_panel_keys_off();
        orch_hawk_reset_notes();
    }

    {   /* the note keys: silent under the splash, as HAWK's */
        uint32_t now = kb_boot_hold ? 0u : fm1_in.notes, ch = now ^ orch_kb_prev, k;
        for (k = 0; ch && k < NKEYS; k++) {
            if (!(ch & (1u << k)))
                continue;
            orch_hawk_panel_key(k, (now >> k) & 1u, ORCH_KEY_VELOCITY);
        }
        orch_kb_prev = now;
    }

    if (midi_in_overflow) {                     /* a broken stream: release everything */
        mi_r = mi_w;
        orch_hawk_reset_notes();
        RING_PUBLISH();
        midi_in_overflow = 0;
        return;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], src = midi_in_source[mi_r % MQ];
        uint32_t status = (pkt >> 8) & 0xFFu;
        mi_r++;
        if ((src == 2u && !sys.midi_trs) || (src == 1u && !(sys.midi_usb & 1u)))
            continue;                           /* (HAWK's GLO page: that input off) */
        if (status < 0x80u)
            continue;
        orch_hawk_midi_message((uint8_t)status, (uint8_t)((pkt >> 16) & 0x7Fu),
                               (uint8_t)((pkt >> 24) & 0x7Fu));
    }
}

/* --- what the panel needs ------------------------------------------------- */

uint32_t orch_hw_ms(void) { return fm1_ms; }

uint32_t orch_hw_buttons(void) { return fm1_in.buttons; }

uint32_t orch_hw_keys(void) { return fm1_in.notes; }

int32_t orch_hw_encoder(unsigned matrix_id)
{
    uint32_t role;
    if (matrix_id >= 7u)
        return 0;
    for (role = 0; role < NE; role++)           /* panel.c: the measured role of each encoder */
        if (panel.enc[role] == matrix_id)
            return panel_enc(role);
    return 0;
}

void orch_hw_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565)
{
    lcd_fill(x, y, w, h, rgb565);
}

/* The ST7789 takes the high byte first and the DMA reads RAM: each row is
 * swapped into a buffer of our own and sent before the next is touched. */
static uint16_t orch_row[240];

void orch_hw_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{
    uint32_t r, i;
    if (!w || !h || x + w > 240u || y + h > 240u)
        return;
    lcd_fresh = 1;
    for (r = 0; r < h; r++) {
        lcd_sync();                             /* the last row is out of orch_row */
        for (i = 0; i < w; i++) {
            uint16_t c = px[r * w + i];
            orch_row[i] = (uint16_t)((c >> 8) | (c << 8));
        }
        lcd_window(x, y + r, x + w - 1u, y + r);
        lcd_data(orch_row, w * 2u);
    }
    lcd_sync();
}

void orch_hw_leds(const uint8_t lit[41])
{
    uint8_t nl[FM1_NCOL];
    uint32_t p, r;
    if (fm1_led_anim_on())                      /* the power-on sweep still runs */
        return;
    memset(nl, 0, sizeof nl);
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++) {
            int8_t id = FM1_KEYMAP[r][p];
            if (id >= 0 && id < 41 && lit[id])
                nl[p] |= (uint8_t)(1u << r);
        }
    for (p = 0; p < FM1_NCOL; p++)
        fm1_led_dim[p] = nl[p];                 /* one byte a column: no dark gap (ui.c) */
}

int orch_hw_store_load(void *data, uint32_t max)
{
    if (!flash_ok)
        return -1;
    return st_load(ORCH_STORE_OBJ, data, max);
}

int orch_hw_store_save(const void *data, uint32_t len)
{
    if (!flash_ok)
        return -1;
    return st_save(ORCH_STORE_OBJ, data, len);
}

void orch_hw_irq_off(void) { (void)irq_save(); }
void orch_hw_irq_on(void) { irq_restore(0); }

/* --- the second core ------------------------------------------------------ */

/* HAWK starts the AC79's second core at boot (main.c, FELUCCA_CPU2, its
 * 2ND CORE setting) and hands it jobs through fm1_cpu1.h: one function and
 * one argument at a time, with its check word, a 10 ms watchdog and a hold
 * for good on any unit that misreads. The orchestra gives it every other
 * sounding voice of each 32-sample chunk (orch_engine.c render_chunk); if it
 * is not running, refuses or stops answering, this core renders them. */
#if FELUCCA_CPU2
static int orch_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    return fm1_c1_on ? fm1_cpu1_run(fn, arg) : 0;
}
static int orch_c1_wait(void) { return fm1_cpu1_wait(); }
#endif

/* --- the main loop -------------------------------------------------------- */

static void orch_hawk_main_frame(void)
{
#if FELUCCA_CPU2
    static uint8_t second_core_given;
    if (!second_core_given) {          /* the core was started before the main loop */
        second_core_given = 1;
        orch_hawk_set_second_core(orch_c1_run, orch_c1_wait);
    }
#endif
    static uint8_t countdown_had_screen;
    uint32_t both = btn_bit(B_OCTDN) | btn_bit(B_OCTUP);
    int ours = (fm1_in.buttons & both) != both;
    if (!ours) {
        countdown_had_screen = 1;
    } else if (countdown_had_screen) {
        countdown_had_screen = 0;
        orch_hawk_panel_repaint();
    }
    orch_hawk_panel_frame(ours);
}
