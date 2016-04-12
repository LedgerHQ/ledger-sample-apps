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
unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

unsigned char usb_enable_request; // request to turn on USB
unsigned int current_text_pos;    // parsing cursor in the text to display
unsigned int text_y;              // current location of the displayed text
unsigned char uiDone;      // notification to come back to the APDU event loop
unsigned char hashTainted; // notification to restart the hash
unsigned int
    currentUiElement; // currently displayed UI element in a set of elements
unsigned char
    element_displayed; // notification of something displayed in a touch handler

// UI currently displayed
enum UI_STATE { UI_IDLE, UI_TEXT, UI_APPROVAL };

enum UI_STATE uiState;

unsigned int io_seproxyhal_touch_exit(bagl_element_t *e);
unsigned int io_seproxyhal_touch_approve(bagl_element_t *e);
unsigned int io_seproxyhal_touch_deny(bagl_element_t *e);

#define MAX_CHARS_PER_LINE 49
#define DEFAULT_FONT BAGL_FONT_OPEN_SANS_LIGHT_13px | BAGL_FONT_ALIGNMENT_LEFT
#define TEXT_HEIGHT 15
#define TEXT_SPACE 4

#define CLA 0x80
#define INS_SIGN 0x02
#define INS_GET_PUBLIC_KEY 0x04
#define P1_LAST 0x80
#define P1_MORE 0x00

const cx_ecfp_private_key_t N_privateKey; // private key in flash. const and N_
                                          // variable name are mandatory here
const unsigned char N_initialized; // initialization marker in flash. const and
                                   // N_ variable name are mandatory here

char lineBuffer[50];
cx_sha256_t hash;

// blank the screen
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

// UI to approve or deny the signature proposal
static const bagl_element_t const bagl_ui_approval[] = {

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 190, 215, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "Deny",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_deny,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 190, 265, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "Approve",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_approve,
     NULL,
     NULL}

};

// UI displayed when no signature proposal has been received
static const bagl_element_t const bagl_ui_idle[] = {

    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 320, 60, 0, 0, BAGL_FILL, 0x1d2028,
         0x1d2028, 0, 0},
    },

    {{BAGL_LABEL, 0x00, 20, 0, 320, 60, 0, 0, BAGL_FILL, 0xFFFFFF, 0x1d2028,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_MIDDLE, 0},
     "Sample Sign",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 190, 215, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "Exit",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_exit,
     NULL,
     NULL}

};

unsigned int io_seproxyhal_touch_exit(bagl_element_t *e) {
    // Go back to the dashboard
    os_sched_exit(0);
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_approve(bagl_element_t *e) {
    unsigned int tx = 0;
    uiDone = 1;
    // Update the hash
    cx_hash(&hash.header, 0, G_io_apdu_buffer + 5, G_io_apdu_buffer[4], NULL);
    if (G_io_apdu_buffer[2] == P1_LAST) {
        // Hash is finalized, send back the signature
        unsigned char result[32];
        cx_hash(&hash.header, CX_LAST, G_io_apdu_buffer, 0, result);
        tx = cx_ecdsa_sign(&N_privateKey, CX_RND_RFC6979 | CX_LAST, CX_SHA256,
                           result, sizeof(result), G_io_apdu_buffer);
	G_io_apdu_buffer[0] &= 0xF0; // discard the parity information 
        hashTainted = 1;
    }
    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
    // Display back the original UX
    uiState = UI_IDLE;
    currentUiElement = 0;
    io_seproxyhal_display(&bagl_ui_erase_all[0]);
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_deny(bagl_element_t *e) {
    // signApprove = 0;
    uiDone = 1;
    hashTainted = 1;
    G_io_apdu_buffer[0] = 0x69;
    G_io_apdu_buffer[1] = 0x85;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, 2);
    // Display back the original UX
    uiState = UI_IDLE;
    currentUiElement = 0;
    io_seproxyhal_display(&bagl_ui_erase_all[0]);
    return 0; // do not redraw the widget
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

                if (G_io_apdu_buffer[0] != CLA) {
                    THROW(0x6E00);
                }

                switch (G_io_apdu_buffer[1]) {
                case INS_SIGN: {
                    if ((G_io_apdu_buffer[2] != P1_MORE) &&
                        (G_io_apdu_buffer[2] != P1_LAST)) {
                        THROW(0x6A86);
                    }
                    if (hashTainted) {
                        cx_sha256_init(&hash);
                        hashTainted = 0;
                    }
                    // Wait for the UI to be completed
                    uiDone = 0;
                    current_text_pos = 0;
                    text_y = 60;
                    G_io_apdu_buffer[5 + G_io_apdu_buffer[4]] = '\0';

                    io_seproxyhal_display(&bagl_ui_erase_all[0]);
                    uiState = UI_TEXT;
                    currentUiElement = 0;

                    // Pump SPI events until the UI has been displayed, then go
                    // back to the original event loop
                    while (!uiDone) {
                        unsigned int rx_len;
                        rx_len = io_seproxyhal_spi_recv(
                            G_io_seproxyhal_spi_buffer,
                            sizeof(G_io_seproxyhal_spi_buffer), 0);
                        io_event(CHANNEL_SPI);
                    }

                    continue;
                } break;

                case INS_GET_PUBLIC_KEY: {
                    cx_ecfp_public_key_t publicKey;
                    cx_ecfp_private_key_t privateKey;
                    os_memmove(&privateKey, &N_privateKey,
                               sizeof(cx_ecfp_private_key_t));
                    cx_ecfp_generate_pair(CX_CURVE_256K1, &publicKey,
                                          &privateKey, 1);
                    os_memmove(G_io_apdu_buffer, publicKey.W, 65);
                    tx = 65;
                    THROW(0x9000);
                } break;

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

void io_seproxyhal_display(const bagl_element_t *element) {
    element_displayed = 1;
    return io_seproxyhal_display_default(element);
}

// Pick the text elements to display
unsigned char display_text_part() {
    unsigned int i;
    WIDE char *text = G_io_apdu_buffer + 5;
    bagl_element_t element;
    if (text[current_text_pos] == '\0') {
        return 0;
    }
    i = 0;
    while ((text[current_text_pos] != 0) && (text[current_text_pos] != '\n') &&
           (i < MAX_CHARS_PER_LINE)) {
        lineBuffer[i++] = text[current_text_pos];
        current_text_pos++;
    }
    if (text[current_text_pos] == '\n') {
        current_text_pos++;
    }
    lineBuffer[i] = '\0';
    os_memset(&element, 0, sizeof(element));
    element.component.type = BAGL_LABEL;
    element.component.x = 4;
    element.component.y = text_y;
    element.component.width = 320;
    element.component.height = TEXT_HEIGHT;
    // element.component.fill = BAGL_FILL;
    element.component.fgcolor = 0x000000;
    element.component.bgcolor = 0xf9f9f9;
    element.component.font_id = DEFAULT_FONT;
    element.text = lineBuffer;
    text_y += TEXT_HEIGHT + TEXT_SPACE;
    io_seproxyhal_display(&element);
    return 1;
}

unsigned char io_event(unsigned char channel) {
    // nothing done with the event, throw an error on the transport layer if
    // needed
    unsigned int offset = 0;

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT: {
        bagl_element_t *elements = NULL;
        unsigned int elementsSize = 0;
        if (uiState == UI_IDLE) {
            elements = bagl_ui_idle;
            elementsSize = sizeof(bagl_ui_idle) / sizeof(bagl_element_t);
        } else if (uiState == UI_APPROVAL) {
            elements = bagl_ui_approval;
            elementsSize = sizeof(bagl_ui_approval) / sizeof(bagl_element_t);
        }
        if (elements != NULL) {
            io_seproxyhal_touch(elements, elementsSize,
                                (G_io_seproxyhal_spi_buffer[4] << 8) |
                                    (G_io_seproxyhal_spi_buffer[5] & 0xFF),
                                (G_io_seproxyhal_spi_buffer[6] << 8) |
                                    (G_io_seproxyhal_spi_buffer[7] & 0xFF),
                                G_io_seproxyhal_spi_buffer[3]);
            if (!element_displayed) {
                goto general_status;
            }
        } else {
            goto general_status;
        }
    } break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        if (uiState == UI_IDLE) {
            if (currentUiElement <
                (sizeof(bagl_ui_idle) / sizeof(bagl_element_t))) {
                io_seproxyhal_display(&bagl_ui_idle[currentUiElement++]);
                break;
            }
        } else if (uiState == UI_TEXT) {
            if (display_text_part()) {
                break;
            } else {
                uiState = UI_APPROVAL;
                currentUiElement = 0;
                io_seproxyhal_display(&bagl_ui_approval[currentUiElement++]);
                break;
            }
        } else if (uiState == UI_APPROVAL) {
            if (currentUiElement <
                (sizeof(bagl_ui_approval) / sizeof(bagl_element_t))) {
                io_seproxyhal_display(&bagl_ui_approval[currentUiElement++]);
                break;
            }
        } else {
            screen_printf("Unknown UI state\n");
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
        element_displayed = 0;
        break;
    }
    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

__attribute__((section(".boot"))) int main(void) {
    // exit critical section
    __asm volatile("cpsie i");

    current_text_pos = 0;
    text_y = 60;
    usb_enable_request = 0;
    hashTainted = 1;
    element_displayed = 0;
    uiState = UI_IDLE;
    currentUiElement = 0;

    // ensure exception will work as planned
    os_boot();

    BEGIN_TRY {
        TRY {
            io_seproxyhal_init();

            // Create the private key if not initialized
            if (N_initialized != 0x01) {
                unsigned char canary;
                cx_ecfp_private_key_t privateKey;
                cx_ecfp_public_key_t publicKey;
                cx_ecfp_generate_pair(CX_CURVE_256K1, &publicKey, &privateKey,
                                      0);
                nvm_write(&N_privateKey, &privateKey, sizeof(privateKey));
                canary = 0x01;
                nvm_write(&N_initialized, &canary, sizeof(canary));
            }

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
