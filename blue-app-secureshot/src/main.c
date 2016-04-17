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

void reset(void) {
    // TODO use the watchdog
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

                case 0xFF: // return to loader
                    os_sched_exit(0);

                // ensure INS is empty otherwise (use secure instruction)
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
                // Unexpected exception => security erase
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY {
            }
        }
        END_TRY;
    }
}

unsigned short io_timeout(unsigned short last_timeout) {
    last_timeout = last_timeout;
    // infinite timeout
    return 1;
}

void paint_component(const bagl_component_t *component) {
    unsigned short length = sizeof(bagl_component_t);
    G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_SCREEN_DISPLAY_STATUS;
    G_io_seproxyhal_spi_buffer[1] = length >> 8;
    G_io_seproxyhal_spi_buffer[2] = length;
    os_memcpy(&G_io_seproxyhal_spi_buffer[3], component,
              sizeof(bagl_component_t));
    io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, length + 3);
}

extern unsigned char _text;
void paint_component_with_txt(const bagl_component_t *component,
                              const char *text) {
    unsigned int text_adr = (unsigned int)text;
    if (text_adr >= (unsigned int)&_text) {
        text_adr = PIC(text_adr);
    }
    unsigned short length =
        sizeof(bagl_component_t) + strlen((unsigned char *)text_adr);
    G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_SCREEN_DISPLAY_STATUS;
    G_io_seproxyhal_spi_buffer[1] = length >> 8;
    G_io_seproxyhal_spi_buffer[2] = length;
    os_memcpy(&G_io_seproxyhal_spi_buffer[3], component,
              sizeof(bagl_component_t));
    os_memcpy(&G_io_seproxyhal_spi_buffer[3 + sizeof(bagl_component_t)],
              (unsigned char *)text_adr, strlen((unsigned char *)text_adr));
    io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, length + 3);
}

void io_seproxyhal_display(const bagl_element_t *element) {
    return io_seproxyhal_display_default(element);
}

#define PLAYER_VELOCITY 5
#define PLAYER_SIZE 8
volatile struct player_s {
    unsigned short x;
    unsigned short y;
    unsigned short next_x;
    unsigned short next_y;
    unsigned int target_destroyed;
    unsigned int velocity;
    int last_direction;
} G_player;

#define SHOT_VELOCITY 10
#define SHOT_COUNT 12
#define SHOT_SIZE 3
volatile struct shot_s {
    unsigned short x;
    unsigned short y;
    unsigned char alive;
    unsigned short next_x;
    unsigned short next_y;
    unsigned char next_alive;
} G_shots[SHOT_COUNT];

#define TARGET_ACCURACY 1
#define TARGET_SIZE 12
#define TARGET_COUNT 10
volatile struct target_s {
    unsigned short x;
    unsigned short y;
    unsigned char alive;
    unsigned short next_x;
    unsigned short next_y;
    unsigned char next_alive;
} G_targets[TARGET_COUNT];

#define EVT_LEFT 1
#define EVT_RIGHT 2
#define EVT_FIRE 4
#define EVT_OFF 8
volatile unsigned int G_events;

#define SCREEN_HEIGHT 480
#define SCREEN_WIDTH 320

unsigned int io_seproxyhal_touch_callback_out(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_callback_over(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_callback_tap(const bagl_element_t *e);

const bagl_element_t erase_screen[] = {
    // type                                 id        x    y    w    h    s  r
    // fill       fg        bg        font icon   text, out, over, touch
    {{BAGL_RECTANGLE, 0x00, 0, 0, 320, 480, 0, 0, BAGL_FILL, 0xF9F9F9, 0xF9F9F9,
      0, 0},
     NULL /*, NULL, NULL, NULL*/},
};

const bagl_element_t controls[] = {
    // type                                 id        x    y    w    h    s  r
    // fill       fg        bg        font icon   text, out, over, touch
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, EVT_LEFT, 10, 10, 40, 75, 2, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_21px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "^",
     0,
     0,
     0,
     io_seproxyhal_touch_callback_tap,
     io_seproxyhal_touch_callback_out,
     io_seproxyhal_touch_callback_over},
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, EVT_RIGHT, 10, 90, 40, 75, 2, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_21px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "v",
     0,
     0,
     0,
     io_seproxyhal_touch_callback_tap,
     io_seproxyhal_touch_callback_out,
     io_seproxyhal_touch_callback_over},
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, EVT_OFF, 10, 310, 40, 40, 2, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_21px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "off",
     0,
     0,
     0,
     io_seproxyhal_touch_callback_tap,
     io_seproxyhal_touch_callback_out,
     io_seproxyhal_touch_callback_over},
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, EVT_FIRE, 10, 380, 40, 75, 2, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_21px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "x",
     0,
     0,
     0,
     io_seproxyhal_touch_callback_tap,
     io_seproxyhal_touch_callback_out,
     io_seproxyhal_touch_callback_over},

    // {{BAGL_CIRCLE                         , 0        , 100, 100,  40,  40, 2,
    // 6, BAGL_FILL, 0x41ccb4, 0xF9F9F9,
    // BAGL_FONT_OPEN_SANS_LIGHT_21px|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE,
    // 0   }, "x"  , 0, 0, 0, io_seproxyhal_touch_callback_tap,
    // io_seproxyhal_touch_callback_out, io_seproxyhal_touch_callback_over},
};

#define CONTROL_ELEMENTS (sizeof(controls) / sizeof(controls[0]))

volatile enum G_state_e {
    ERASE_SCREEN,
    ERASE_PLAYER,
    DRAW_PLAYER,
    ERASE_TARGET,
    DRAW_TARGET,
    ERASE_SHOT,
    DRAW_SHOT,
    DRAW_CONTROLS,
    DRAW_SCORE,
} G_state;
volatile unsigned int G_draw_buttons;
volatile unsigned int G_state_index;

unsigned int io_seproxyhal_touch_callback_out(const bagl_element_t *e) {
    return 0;
}

unsigned int io_seproxyhal_touch_callback_over(const bagl_element_t *e) {
    // off must be tapped, not just hovered
    if (e->component.userid != EVT_OFF) {
        G_events |= e->component.userid;
    }
    return 0;
}

unsigned int io_seproxyhal_touch_callback_tap(const bagl_element_t *e) {
    // for off event, need a release over the button to avoid off when just
    // passing over
    if (e->component.userid == EVT_OFF) {
        os_sched_exit(0);
    }
    return 0; // never redraw
}

unsigned char io_event(unsigned char channel) {
    bagl_component_t c;
    // nothing done with the event, throw an error on the transport layer if
    // needed
    unsigned int offset = 0;

    // better to avoid strange behavior with BAGL_NONE components !
    memset(&c, 0, sizeof(c));

    // just reply "amen"
    // add a "pairing ok" tag if necessary
    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        io_seproxyhal_touch(controls, CONTROL_ELEMENTS,
                            (G_io_seproxyhal_spi_buffer[4] << 8) |
                                (G_io_seproxyhal_spi_buffer[5] & 0xFF),
                            (G_io_seproxyhal_spi_buffer[6] << 8) |
                                (G_io_seproxyhal_spi_buffer[7] & 0xFF),
                            // map events (always consider releasing)
                            G_io_seproxyhal_spi_buffer[3]);
        // no repaint here, never !
        goto general_status;

    case SEPROXYHAL_TAG_TICKER_EVENT: {
        unsigned int i, j;

        // go off when asked
        if (G_events & EVT_OFF) {
            os_sched_exit(0);
            break;
        }

        // prepare frame (animate and check shots)

        // update target next state
        for (i = 0; i < TARGET_COUNT; i++) {
            G_targets[i].next_x = G_targets[i].x;
            G_targets[i].next_y = G_targets[i].y;
            G_targets[i].next_alive = G_targets[i].alive;
        }

        // handle shot vs targets
        for (i = 0; i < SHOT_COUNT; i++) {
            // TODO homing missiles :p
            G_shots[i].next_x = G_shots[i].x + SHOT_VELOCITY;
            G_shots[i].next_y = G_shots[i].y;
            G_shots[i].next_alive = G_shots[i].alive;

            if (G_shots[i].alive) {
                for (j = 0; j < TARGET_COUNT; j++) {
                    // TODO radius check instead of basic window
                    if (G_targets[j].alive &&
                        G_targets[j].y + TARGET_ACCURACY + TARGET_SIZE >=
                            G_shots[i].y &&
                        G_targets[j].y - TARGET_ACCURACY <= G_shots[i].y &&
                        G_targets[j].x - 2 <= G_shots[i].x) {
                        G_targets[j].next_alive = 0;
                        G_shots[i].next_alive = 0;
                        G_player.target_destroyed++;
                        break;
                    }
                }

                // remove home runners
                if (G_shots[i].next_alive &&
                    G_shots[i].next_x >= SCREEN_WIDTH) {
                    G_shots[i].next_alive = 0;
                }
            }
        }

        // by default, the player won't move
        G_player.next_x = G_player.x;
        G_player.next_y = G_player.y;

        // go left
        if (G_events & EVT_LEFT) {
            // handle velocity
            if (G_player.last_direction < 0) {
                G_player.velocity += 1;
            } else {
                G_player.velocity = PLAYER_VELOCITY;
            }
            G_player.last_direction = -1;

            // next player position
            if (G_player.next_y < G_player.velocity) {
                G_player.next_y = G_player.velocity;
            }
            G_player.next_y -= G_player.velocity;
        }

        // go right
        if (G_events & EVT_RIGHT) {
            // handle velocity
            if (G_player.last_direction > 0) {
                G_player.velocity += 1;
            } else {
                G_player.velocity = PLAYER_VELOCITY;
            }
            G_player.last_direction = 1;

            // next player position
            if (G_player.next_y > SCREEN_HEIGHT - G_player.velocity) {
                G_player.next_y = SCREEN_HEIGHT - G_player.velocity;
            }
            G_player.next_y += G_player.velocity;
        }

        // reset velocity on stop
        if (G_events & (EVT_LEFT | EVT_RIGHT) == 0) {
            G_player.velocity = PLAYER_VELOCITY;
        }

        // create shots
        if (G_events & EVT_FIRE) {
            for (i = 0; i < SHOT_COUNT; i++) {
                if (!G_shots[i].alive) {
                    G_shots[i].next_alive = 1;
                    G_shots[i].next_x = G_player.x + 10;
                    G_shots[i].next_y =
                        G_player.y + PLAYER_SIZE / 2 - SHOT_SIZE / 2;
                    break;
                }
            }
        }

        // recreate targets
        for (i = 0; i < TARGET_COUNT; i++) {
        new_pos:
            if (!G_targets[i].alive) {
                G_targets[i].next_alive = 1;
                G_targets[i].next_x = 300 - (cx_rng_u8() % 100);
                G_targets[i].next_y =
                    20 + (cx_rng_u8() * 10 + G_player.y) % (SCREEN_HEIGHT - 20);

                // TODO check that the target does not overlap another, or redo
            }
        }

        // consume events
        G_events = 0;

        // refresh frame
        G_state = ERASE_PLAYER;

        // no break is intentional
    }
    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:

        // process structures to be erased/painted.
        switch (G_state) {
        case ERASE_PLAYER:
            if (G_player.next_x != G_player.x ||
                G_player.next_y != G_player.y) {
                c.type = BAGL_RECTANGLE;
                c.fill = BAGL_FILL;
                c.fgcolor = 0xF9F9F9;
                c.bgcolor = 0xF9F9F9;
                c.x = G_player.x;
                c.y = G_player.y;
                c.stroke = 0;
                c.width = c.height = PLAYER_SIZE;
                c.radius = PLAYER_SIZE / 2; // draw a filled circle
                paint_component(&c);
                G_state = DRAW_PLAYER;
            } else {
                c.type = BAGL_NONE;
                paint_component(&c);
                G_state = ERASE_TARGET;
                G_state_index = 0;
            }
            G_player.x = G_player.next_x;
            G_player.y = G_player.next_y;
            break;

        case DRAW_PLAYER:
            c.type = BAGL_BUTTON;
            c.fill = BAGL_FILL;
            c.fgcolor = 0x00FF00;
            c.bgcolor = 0xF9F9F9;
            c.x = G_player.x;
            c.y = G_player.y;
            c.stroke = 0;
            c.width = c.height = PLAYER_SIZE;
            c.radius = PLAYER_SIZE / 2; // draw a filled circle
            paint_component(&c);
            G_state = ERASE_TARGET;
            G_state_index = 0;
            break;

        case ERASE_TARGET:
            if (G_targets[G_state_index].alive &&
                !G_targets[G_state_index].next_alive) {
                c.type = BAGL_RECTANGLE;
                c.fill = BAGL_FILL;
                c.fgcolor = 0xF9F9F9;
                c.bgcolor = 0xF9F9F9;
                c.x = G_targets[G_state_index].x;
                c.y = G_targets[G_state_index].y;
                c.stroke = 0;
                c.width = c.height = TARGET_SIZE;
                c.radius = 0;
                paint_component(&c);
            } else {
                c.type = BAGL_NONE;
                paint_component(&c);
            }
            G_targets[G_state_index].x = G_targets[G_state_index].next_x;
            G_targets[G_state_index].y = G_targets[G_state_index].next_y;
            G_targets[G_state_index].alive =
                G_targets[G_state_index].next_alive;
            G_state = DRAW_TARGET;
            break;

        case DRAW_TARGET:
            // if a shot reach this target, then don't print it and mark it
            // dead, also mark the shot dead

            // draw current target
            if (G_targets[G_state_index].alive) {
                c.type = BAGL_RECTANGLE;
                c.fill = 0; // BAGL_FILL;
                c.fgcolor = 0xFF0000;
                c.bgcolor = 0xF9F9F9;
                c.x = G_targets[G_state_index].x;
                c.y = G_targets[G_state_index].y;
                c.stroke = 1;
                c.width = c.height = TARGET_SIZE;
                c.radius = TARGET_SIZE / 2; // draw a filled circle
                paint_component(&c);
            } else {
                c.type = BAGL_NONE;
                paint_component(&c);
            }

            // erase next target if remaining
            if (G_state_index < TARGET_COUNT - 1) {
                G_state = ERASE_TARGET;
                G_state_index++;
                break;
            }

            G_state = ERASE_SHOT;
            G_state_index = 0;
            break;

        case ERASE_SHOT:
            if (G_shots[G_state_index].alive) {
                c.type = BAGL_RECTANGLE;
                c.fill = BAGL_FILL;
                c.fgcolor = 0xF9F9F9;
                c.bgcolor = 0xF9F9F9;
                c.x = G_shots[G_state_index].x;
                c.y = G_shots[G_state_index].y;
                c.stroke = 0;
                c.width = c.height = SHOT_SIZE;
                c.radius = 0;
                paint_component(&c);
            } else {
                // dummy paint to ensure receiving a following display_processed
                // event
                c.type = BAGL_NONE;
                paint_component(&c);
            }

            G_shots[G_state_index].x = G_shots[G_state_index].next_x;
            G_shots[G_state_index].y = G_shots[G_state_index].next_y;
            G_shots[G_state_index].alive = G_shots[G_state_index].next_alive;
            G_state = DRAW_SHOT;
            break;

        case DRAW_SHOT:
            // if a shot reach this target, then don't print it and mark it
            // dead, also mark the shot dead

            // draw current shot
            if (G_shots[G_state_index].alive) {
                c.type = BAGL_BUTTON;
                c.fill = 0; // BAGL_FILL;
                c.fgcolor = 0x000000;
                c.bgcolor = 0xFFFF00;
                c.x = G_shots[G_state_index].x;
                c.y = G_shots[G_state_index].y;
                c.stroke = 1;
                c.width = c.height = SHOT_SIZE;
                c.radius = SHOT_SIZE / 2; // draw a filled circle
                paint_component(&c);
            } else {
                c.type = BAGL_NONE;
                paint_component(&c);
            }
            // erase next shot if remaining
            if (G_state_index < SHOT_COUNT - 1) {
                G_state = ERASE_SHOT;
                G_state_index++;
                break;
            }

            // then will refresh game panel and score
            G_state = DRAW_CONTROLS;
            if (G_draw_buttons) {
                G_state_index = 0;
            } else {
                // consider buttons already drawn
                G_state_index = CONTROL_ELEMENTS;
            }
            break;

        case DRAW_CONTROLS:
            if (G_state_index < CONTROL_ELEMENTS) {
                io_seproxyhal_display(&controls[G_state_index++]);
                break;
            } else {
                // control buttons drawn
                G_draw_buttons = 0;

                // display score
                c.type = BAGL_LABEL;
                c.fill = BAGL_NOFILL;
                c.fgcolor = 0x000000;
                c.bgcolor = 0xF9F9F9;
                c.font_id =
                    BAGL_FONT_ALIGNMENT_RIGHT | BAGL_FONT_OPEN_SANS_LIGHT_21px;
                c.x = SCREEN_WIDTH - 100;
                c.y = 0;
                c.width = 100;
                c.height = 40;
                c.stroke = 0;
                c.radius = 0;
                char buffer[5];
                unsigned int score = G_player.target_destroyed;
                buffer[0] = '0' + ((score / 1000) % 10);
                buffer[1] = '0' + ((score / 100) % 10);
                buffer[2] = '0' + ((score / 10) % 10);
                buffer[3] = '0' + ((score / 1) % 10);
                buffer[4] = '\0';
                paint_component_with_txt(&c, buffer);
                G_state = DRAW_SCORE;
            }
            break;

        case DRAW_SCORE:
        case ERASE_SCREEN:
        general_status:
            // send a general status last command
            offset = 0;
            G_io_seproxyhal_spi_buffer[offset++] =
                SEPROXYHAL_TAG_GENERAL_STATUS;
            G_io_seproxyhal_spi_buffer[offset++] = 0;
            G_io_seproxyhal_spi_buffer[offset++] = 2;
            G_io_seproxyhal_spi_buffer[offset++] =
                SEPROXYHAL_TAG_GENERAL_STATUS_LAST_COMMAND >> 8;
            G_io_seproxyhal_spi_buffer[offset++] =
                SEPROXYHAL_TAG_GENERAL_STATUS_LAST_COMMAND;
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, offset);
            break;
        }
        break;

    // unknown events are acknowledged
    default:
        goto general_status;
    }

    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

unsigned char
    G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B]; // to contain an
                                                             // APDU, but we
                                                             // have some ram,
                                                             // sooooo

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

const unsigned char const const_anti_align_0x1000[] = {
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
};

__attribute__((section(".boot"))) int main(void) {
    // exit critical section
    __asm volatile("cpsie i");

    // ensure exception will work as planned
    os_boot();

    BEGIN_TRY {
        TRY {
            io_seproxyhal_init();

            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_SET_TICKER_INTERVAL;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 2;
            G_io_seproxyhal_spi_buffer[3] = 0;
            G_io_seproxyhal_spi_buffer[4] =
                const_anti_align_0x1000[sizeof(const_anti_align_0x1000) - 1];
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 5);

            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_SET_TICKER_INTERVAL;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 2;
            G_io_seproxyhal_spi_buffer[3] = 0;
            G_io_seproxyhal_spi_buffer[4] = 50;
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 5);

            // initialize the game panel, targets and player
            {
                unsigned int i;

                os_memset(&G_player, 0, sizeof(G_player));
                os_memset(G_shots, 0, sizeof(G_shots));
                os_memset(G_targets, 0, sizeof(G_targets));
                // consume events
                G_events = 0;
                // draw buttons only once ! to avoid flickering
                G_draw_buttons = 1;
                G_state_index = 0;

                G_player.x = 100;
                G_player.y = 200;
                G_player.velocity = PLAYER_VELOCITY;
                G_player.last_direction = 0;

                for (i = 0; i < TARGET_COUNT; i++) {
                    G_targets[i].x = 300 - (cx_rng_u8() % 100);
                    G_targets[i].y =
                        20 +
                        (cx_rng_u8() * 10 + G_player.y) % (SCREEN_HEIGHT - 20);
                    G_targets[i].alive = 1;
                }
            }

            // erase loader screen (once only)
            G_state = ERASE_SCREEN;
            io_seproxyhal_display(&erase_screen[0]);

            sample_main();
        }
        CATCH_OTHER(e) {
        }
        FINALLY {
        }
    }
    END_TRY;
}
