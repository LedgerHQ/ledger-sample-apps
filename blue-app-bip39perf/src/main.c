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

#include "os_io_seproxyhal.h"
#include "string.h"

#include "alt.h"

unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

unsigned char usb_enable_request;
unsigned int current_element;

unsigned int io_seproxyhal_touch_exit(bagl_element_t *e);

#define INS_MNEMONIC 0x02
#define INS_MNEMONIC_ALT 0x04
#define INS_SHA512 0x06
#define INS_SHA512_ALT 0x08

#define TEST_ROUNDS 500

#define HMAC_LENGTH 64

static const char BIP39_MNEMONIC[] = "mnemonic";
#define BIP39_MNEMONIC_LENGTH 8
#define BIP39_PBKDF2_ROUNDS 2048

void btchip_pbkdf2(
  unsigned char* password, unsigned short passwordlen,
  unsigned char* salt,  unsigned short saltlen,
  unsigned int iterations,
  unsigned char* out, unsigned int outLength) {
    unsigned long int i;
    unsigned int j;
    unsigned char f[HMAC_LENGTH], g[HMAC_LENGTH];
    unsigned int blocks = outLength / HMAC_LENGTH;
    if (outLength & (HMAC_LENGTH - 1)) {
      blocks++;
    }
    cx_hmac_sha512_t sha512;
    cx_hmac_sha512_init(&sha512, password, passwordlen);
    for (i=1; i<=blocks; i++) {
      
      salt[saltlen - 4] = (i >> 24) & 0xff;
      salt[saltlen - 3] = (i >> 16) & 0xff;
      salt[saltlen - 2] = (i >> 8) & 0xff;
      salt[saltlen - 1] = i & 0xff;      
      cx_hmac((cx_hmac_t *)&sha512, CX_LAST, salt, saltlen, g);
      os_memmove(f, g, HMAC_LENGTH);
      for (j=1; j<iterations; j++) {
        cx_hmac((cx_hmac_t *)&sha512, CX_LAST, g, HMAC_LENGTH, g);
        os_xor(f, f, g, HMAC_LENGTH);
      }
      if (i == (blocks - 1) && (outLength & (HMAC_LENGTH - 1))) {
        os_memmove(out + HMAC_LENGTH * (i - 1), f, outLength & (HMAC_LENGTH - 1));
      }
      else {
        os_memmove(out + HMAC_LENGTH * (i - 1), f, HMAC_LENGTH);
      }
    }
  }  

void btchip_mnemonic_to_seed(unsigned char *mnemonic, unsigned short mnemonicLength, unsigned char *passphrase, unsigned short passphraseLength, unsigned char *seed, unsigned char *workBuffer) {
  unsigned char key[128];
  if (passphraseLength > 200) {
    THROW (INVALID_PARAMETER);
  }
  if (mnemonicLength > 128) {
    cx_sha512_t hash;
    cx_sha512_init(&hash);
    cx_hash(&hash.header, CX_LAST, mnemonic, mnemonicLength, key);
    mnemonicLength = 64;
  }
  else {
    os_memmove(key, mnemonic, mnemonicLength);
  }
  os_memmove(workBuffer, BIP39_MNEMONIC, BIP39_MNEMONIC_LENGTH);
  os_memmove(workBuffer + BIP39_MNEMONIC_LENGTH, passphrase, passphraseLength);
  btchip_pbkdf2(key, mnemonicLength, workBuffer, BIP39_MNEMONIC_LENGTH + passphraseLength + 4, BIP39_PBKDF2_ROUNDS, seed, 64);
}

void btchip_mnemonic_to_seed_alt(unsigned char *mnemonic, unsigned short mnemonicLength, unsigned char *passphrase, unsigned short passphraseLength, unsigned char *seed, unsigned char *workBuffer) {
  unsigned char key[128];
  if (passphraseLength > 200) {
    THROW (INVALID_PARAMETER);
  }
  if (mnemonicLength > 128) {
    cx_sha512_t hash;
    cx_sha512_init(&hash);
    cx_hash(&hash.header, CX_LAST, mnemonic, mnemonicLength, key);
    mnemonicLength = 64;
  }
  else {
    os_memmove(key, mnemonic, mnemonicLength);
  }
  os_memmove(workBuffer, BIP39_MNEMONIC, BIP39_MNEMONIC_LENGTH);
  os_memmove(workBuffer + BIP39_MNEMONIC_LENGTH, passphrase, passphraseLength);
  alt_pbkdf2(key, mnemonicLength, workBuffer, BIP39_MNEMONIC_LENGTH + passphraseLength + 4, BIP39_PBKDF2_ROUNDS, seed, 64);
}


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
     "BIP39 Perf",
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

};

unsigned int io_seproxyhal_touch_exit(bagl_element_t *e) {
    // Go back to the dashboard
    os_sched_exit(0);
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

                    case INS_MNEMONIC: {
                        uint8_t work[200];
                        uint8_t seed[64];
                        btchip_mnemonic_to_seed(G_io_apdu_buffer + 5, G_io_apdu_buffer[4], G_io_apdu_buffer, 0, seed, work);
                        os_memmove(G_io_apdu_buffer, seed, 64);
                        tx = 64;
            			THROW(0x9000);
                    }
                    break;

                    case INS_MNEMONIC_ALT: {
                        uint8_t work[200];
                        uint8_t seed[64];
                        btchip_mnemonic_to_seed_alt(G_io_apdu_buffer + 5, G_io_apdu_buffer[4], G_io_apdu_buffer, 0, seed, work);
                        os_memmove(G_io_apdu_buffer, seed, 64);
                        tx = 64;
                        THROW(0x9000);
                    }
                    break;

                    case INS_SHA512: {                        
                        cx_sha512_t hash;
                        uint8_t result[64];
                        uint32_t i;
                        for (i=0; i<TEST_ROUNDS; i++) {
                            cx_sha512_init(&hash);
                            cx_hash(&hash.header, CX_LAST, G_io_apdu_buffer + 5, G_io_apdu_buffer[4], result);
                        }
                        os_memmove(G_io_apdu_buffer, result, 64);
                        tx = 64;
                        THROW(0x9000);
                    }
                    break;

                    case INS_SHA512_ALT: {                        
                        mbedtls_sha512_context hash;
                        uint8_t result[64];
                        uint32_t i;
                        for (i=0; i<TEST_ROUNDS; i++) {
                            mbedtls_sha512_init(&hash);
                            mbedtls_sha512_starts(&hash);
                            mbedtls_sha512_update(&hash, G_io_apdu_buffer + 5 , G_io_apdu_buffer[4]);
                            mbedtls_sha512_finish(&hash, result);
                        }
                        os_memmove(G_io_apdu_buffer, result, 64);
                        tx = 64;
                        THROW(0x9000);
                    }
                    break;                    

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

            // send BLE power on (default parameters)
            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_BLE_RADIO_POWER;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 1;
            G_io_seproxyhal_spi_buffer[3] = 3;
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 4);
            usb_enable_request = 1;
            io_seproxyhal_display(&bagl_ui_erase_all[0]);

            sample_main();
        }
        CATCH_OTHER(e) {
        }
        FINALLY {
        }
    }
    END_TRY;
}
