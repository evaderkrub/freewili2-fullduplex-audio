// src/platform/ioexp_pcal6524.c — PCAL6524 glue for the USB-device firmware.
// Drives the display-side expander to a minimal safe state (I2C pulls on, USB
// D+ pull-up off) and exposes ioexp_usb_dplus() to assert/de-assert the USB
// attach by gating the 1.5K D+ resistor on P2_1.
#include "platform/ioexp_pcal6524.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define IOEXP_I2C   i2c1
#define IOEXP_ADDR  0x23   // display-side expander (decimal 35)
#define REG_OUTPUT0 0x04   // output ports 0x04..0x06 (auto-increment)
#define REG_CONFIG0 0x0C   // direction ports 0x0C..0x0E (1 = input)

// Shadow of the 24 output-pin values so set/commit can re-pack + rewrite.
static uint8_t s_values[IOEXP_PIN_COUNT];

static bool write_ports(uint8_t base, const uint8_t b[3]) {
    uint8_t msg[4] = { base, b[0], b[1], b[2] };
    return i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, msg, 4, false) == 4;
}

bool ioexp_commit(void) {
    uint8_t packed[3];
    ioexp_pack(s_values, packed);
    bool ok = write_ports(REG_OUTPUT0, packed);
    if (!ok) DIAG("ioexp: output write NAK (0x%02x)\n", IOEXP_ADDR);
    return ok;
}

void ioexp_usb_dplus_drive(bool level) {
    s_values[IOEXP_USB_DPLUS_EN] = level ? 1 : 0;
    ioexp_commit();
    DIAG("ioexp: P2_1 D+ pin -> %d\n", level ? 1 : 0);
}

void ioexp_mic_pwr_drive(bool on) {
    s_values[IOEXP_MIC_PWR] = on ? 1 : 0;
    bool ok = ioexp_commit();
    DIAG("ioexp: MIC_PWR (P1_7) -> %d (%s)\n", on ? 1 : 0, ok ? "ACK" : "NAK");
}

bool ioexp_write_raw(const uint8_t values[IOEXP_PIN_COUNT],
                     const uint8_t dir_input[IOEXP_PIN_COUNT]) {
    uint8_t out[3], dir[3];
    for (int i = 0; i < IOEXP_PIN_COUNT; i++) s_values[i] = values[i];
    ioexp_pack(values, out);
    ioexp_pack(dir_input, dir);
    bool ok = write_ports(REG_OUTPUT0, out);
    ok = write_ports(REG_CONFIG0, dir) && ok;
    return ok;
}

bool ioexp_init(void) {
    // Bring up I2C1.
    gpio_set_function(PIN_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C1_SDA);
    gpio_pull_up(PIN_I2C1_SCL);
    i2c_init(IOEXP_I2C, 400 * 1000);

    uint8_t dir_input[IOEXP_PIN_COUNT], out[3], dir[3];
    ioexp_defaults(s_values, dir_input);
    ioexp_pack(s_values, out);
    ioexp_pack(dir_input, dir);

    // Outputs FIRST, then directions: a pin only starts driving the latched
    // value the moment its direction flips to output (glitch-free).
    bool ok = write_ports(REG_OUTPUT0, out);
    ok = write_ports(REG_CONFIG0, dir) && ok;

    DIAG("ioexp: init %s out=%02x%02x%02x dir=%02x%02x%02x\n",
         ok ? "ok" : "FAILED", out[0], out[1], out[2], dir[0], dir[1], dir[2]);
    return ok;
}
