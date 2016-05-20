/*******************************************************************************
*   Ledger Blue
*   (c) 2016 Ledger
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "os.h"
#include "cx.h"
#include <stdbool.h>

#include "os_io_seproxyhal.h"
#include "string.h"
unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

unsigned char usb_enable_request;
unsigned int current_element;

unsigned int io_seproxyhal_touch_exit(bagl_element_t *e);
unsigned int io_seproxyhal_touch_next(bagl_element_t *e);
unsigned int io_seproxyhal_touch_auth(bagl_element_t *e);
bool derive(void);

char address[100];
unsigned int path[5];

static const uint8_t const NOT_AVAILABLE[] = "Not available";

static const bagl_element_t const bagl_ui_erase_all[] = {
    {{BAGL_RECTANGLE, 0x00, 0, 60, 320, 420, 0, 0, BAGL_FILL, 0xf9f9f9,
      0xf9f9f9, 0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};

static const bagl_element_t const bagl_ui_sample[] = {
    // type                                 id    x    y    w    h    s  r  fill
    // fg        bg        font
    // icon     text,         area, overfgcolor, overbgcolor, tap, over, out
    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 320, 60, 0, 0, BAGL_FILL, 0x1d2028,
         0x1d2028, 0, 0},
    },

    {{BAGL_LABEL, 0x00, 20, 0, 320, 60, 0, 0, BAGL_FILL, 0xFFFFFF, 0x1d2028,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_MIDDLE, 0},
     "Hello Perso",
     0,

     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABEL, 0x00, 20, 100, 320, 60, 0, 0, 0, 0, 0xF9F9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_13px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     address,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 165, 225, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "EXIT",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_exit,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 165, 280, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "NEXT",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_next,
     NULL,
     NULL},

#if 0
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 165, 335, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "AUTH",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_auth,
     NULL,
     NULL},
#endif

};

unsigned int io_seproxyhal_touch_exit(bagl_element_t *e) {
    // Go back to the dashboard
    os_sched_exit(0);
    return 0;
}

unsigned int io_seproxyhal_touch_next(bagl_element_t *e) {
    path[4]++;
    if (!derive()) {
        path[4]--;
    }
    current_element = 0;
    io_seproxyhal_display(&bagl_ui_erase_all[0]);
    return 0;
}

unsigned int io_seproxyhal_touch_auth(bagl_element_t *e) {
    if (!os_global_pin_is_validated()) {
        bolos_ux_params_t params;
        os_memset(&params, 0, sizeof(params));
        params.ux_id = BOLOS_UX_VALIDATE_PIN;
        os_ux_blocking(&params);
        current_element = 0;
        io_seproxyhal_display(&bagl_ui_erase_all[0]);
    }
    return 0;
}

void reset(void) {
}

unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len) {
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        } else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}

void sample_main(void) {
    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    // DESIGN NOTE: the bootloader ignores the way APDU are fetched. The only
    // goal is to retrieve APDU.
    // When APDU are to be fetched from multiple IOs, like NFC+USB+BLE, make
    // sure the io_event is called with a
    // switch event, before the apdu is replied to the bootloader. This avoid
    // APDU injection faults.
    for (;;) {
        volatile unsigned short sw = 0;

        BEGIN_TRY {
            TRY {
                rx = tx;
                tx = 0; // ensure no race in catch_other if io_exchange throws
                        // an error
                rx = io_exchange(CHANNEL_APDU | flags, rx);

                // no apdu received, well, reset the session, and reset the
                // bootloader configuration
                if (rx == 0) {
                    THROW(0x6982);
                }

                if (G_io_apdu_buffer[0] != 0x80) {
                    THROW(0x6E00);
                }

                // unauthenticated instruction
                switch (G_io_apdu_buffer[1]) {
                case 0x00: // reset
                    flags |= IO_RESET_AFTER_REPLIED;
                    THROW(0x9000);
                    break;

                case 0x01: // case 1
                    THROW(0x9000);
                    break;

                case 0x02: // echo
                    tx = rx;
                    THROW(0x9000);
                    break;

                case 0xFF: // return to dashboard
                    goto return_to_dashboard;

                default:
                    THROW(0x6D00);
                    break;
                }
            }
            CATCH_OTHER(e) {
                switch (e & 0xF000) {
                case 0x6000:
                case 0x9000:
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY {
            }
        }
        END_TRY;
    }

return_to_dashboard:
    return;
}

unsigned int element_displayed;
void io_seproxyhal_display(const bagl_element_t *element) {
    element_displayed = 1;
    return io_seproxyhal_display_default(element);
}

unsigned char io_event(unsigned char channel) {
    bagl_component_t c;
    // nothing done with the event, throw an error on the transport layer if
    // needed
    unsigned int offset = 0;

    // can't have more than one tag in the reply, not supported yet.
    element_displayed = 0;
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        io_seproxyhal_touch(bagl_ui_sample,
                            sizeof(bagl_ui_sample) / sizeof(bagl_element_t),
                            (G_io_seproxyhal_spi_buffer[4] << 8) |
                                (G_io_seproxyhal_spi_buffer[5] & 0xFF),
                            (G_io_seproxyhal_spi_buffer[6] << 8) |
                                (G_io_seproxyhal_spi_buffer[7] & 0xFF),
                            G_io_seproxyhal_spi_buffer[3]);
        // no repaint here, never !
        if (!element_displayed) {
            goto general_status;
        }
        break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        if (current_element <
            (sizeof(bagl_ui_sample) / sizeof(bagl_element_t))) {
            io_seproxyhal_display(&bagl_ui_sample[current_element++]);
            break;
        }
        if (usb_enable_request) {
            io_usb_enable(1);
            usb_enable_request = 0;
        }
        goto general_status;

    // unknown events are acknowledged
    default:
    general_status:
        // send a general status last command
        offset = 0;
        G_io_seproxyhal_spi_buffer[offset++] = SEPROXYHAL_TAG_GENERAL_STATUS;
        G_io_seproxyhal_spi_buffer[offset++] = 0;
        G_io_seproxyhal_spi_buffer[offset++] = 2;
        G_io_seproxyhal_spi_buffer[offset++] =
            SEPROXYHAL_TAG_GENERAL_STATUS_LAST_COMMAND >> 8;
        G_io_seproxyhal_spi_buffer[offset++] =
            SEPROXYHAL_TAG_GENERAL_STATUS_LAST_COMMAND;
        io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, offset);
        break;
    }
    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

unsigned char const BASE58ALPHABET[] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'};

unsigned int encode_base58(unsigned char *in, unsigned int length,
                           unsigned char *out, unsigned int maxoutlen) {
    unsigned char tmp[164];
    unsigned char buffer[164];
    unsigned char j;
    unsigned char startAt;
    unsigned char zeroCount = 0;
    if (length > sizeof(tmp)) {
        THROW(INVALID_PARAMETER);
    }
    os_memmove(tmp, in, length);
    while ((zeroCount < length) && (tmp[zeroCount] == 0)) {
        ++zeroCount;
    }
    j = 2 * length;
    startAt = zeroCount;
    while (startAt < length) {
        unsigned short remainder = 0;
        unsigned char divLoop;
        for (divLoop = startAt; divLoop < length; divLoop++) {
            unsigned short digit256 = (unsigned short)(tmp[divLoop] & 0xff);
            unsigned short tmpDiv = remainder * 256 + digit256;
            tmp[divLoop] = (unsigned char)(tmpDiv / 58);
            remainder = (tmpDiv % 58);
        }
        if (tmp[startAt] == 0) {
            ++startAt;
        }
        buffer[--j] = (unsigned char)BASE58ALPHABET[remainder];
    }
    while ((j < (2 * length)) && (buffer[j] == BASE58ALPHABET[0])) {
        ++j;
    }
    while (zeroCount-- > 0) {
        buffer[--j] = BASE58ALPHABET[0];
    }
    length = 2 * length - j;
    if (maxoutlen < length) {
        THROW(EXCEPTION_OVERFLOW);
    }
    os_memmove(out, (buffer + j), length);
    return length;
}

unsigned char const HEXDIGITS[] = "0123456789ABCDEF";

void dump(unsigned char *buffer, unsigned int size, unsigned char *target) {
    unsigned int i;
    for (i = 0; i < size; i++) {
        target[2 * i] = HEXDIGITS[(buffer[i] >> 4) & 0x0f];
        target[2 * i + 1] = HEXDIGITS[buffer[i] & 0x0f];
    }
    target[2 * size] = '\0';
}

bool derive() {
    cx_ecfp_private_key_t privateKey;
    cx_ecfp_public_key_t publicKey;
    union {
        cx_sha256_t shasha;
        cx_ripemd160_t riprip;
    } u;
    cx_ripemd160_t ripemd;
    unsigned char privateKeyData[32];
    unsigned char tmp[25];
    unsigned int length;

    if (!os_global_pin_is_validated()) {
        os_memmove(address, NOT_AVAILABLE, sizeof(NOT_AVAILABLE));
        return false;
    }

    os_perso_derive_seed_bip32(path, 5, privateKeyData, NULL);

    cx_ecdsa_init_private_key(CX_CURVE_256K1, privateKeyData, 32, &privateKey);
    cx_ecfp_generate_pair(CX_CURVE_256K1, &publicKey, &privateKey, 1);
    publicKey.W[0] = ((publicKey.W[64] & 1) ? 0x03 : 0x02);
    cx_sha256_init(&u.shasha);
    cx_hash(&u.shasha.header, CX_LAST, publicKey.W, 33, privateKeyData);
    cx_ripemd160_init(&u.riprip);
    cx_hash(&u.riprip.header, CX_LAST, privateKeyData, 32, tmp + 1);
    tmp[0] = 0;
    cx_sha256_init(&u.shasha);
    cx_hash(&u.shasha.header, CX_LAST, tmp, 21, privateKeyData);
    cx_sha256_init(&u.shasha);
    cx_hash(&u.shasha.header, CX_LAST, privateKeyData, 32, privateKeyData);
    os_memmove(tmp + 21, privateKeyData, 4);
    length = encode_base58(tmp, sizeof(tmp), address, sizeof(address));
    address[length] = '\0';
    return true;
}

__attribute__((section(".boot"))) int main(void) {
    // exit critical section
    __asm volatile("cpsie i");

    current_element = 0;
    usb_enable_request = 0;

    // ensure exception will work as planned
    os_boot();

    BEGIN_TRY {
        TRY {
            io_seproxyhal_init();

            screen_printf("HelloPerso\n");

#if 0            
            os_global_pin_invalidate();
#endif

            // send BLE power on (default parameters)
            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_BLE_RADIO_POWER;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 1;
            G_io_seproxyhal_spi_buffer[3] = 3;
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 4);
            usb_enable_request = 1;

            path[0] = 44 | 0x80000000;
            path[1] = 0 | 0x80000000;
            path[2] = 0 | 0x80000000;
            path[3] = 0;
            path[4] = 0;

            derive();
            io_seproxyhal_display(&bagl_ui_erase_all[0]);

            sample_main();
        }
        CATCH_OTHER(e) {
        }
        FINALLY {
        }
    }
    END_TRY;

    BEGIN_TRY_L(outError) {
        TRY_L(outError) {
            screen_printf("outError\n");
            os_sched_exit(0);
        }
        FINALLY_L(outError) {
        }
    }
    END_TRY_L(outError);
}
