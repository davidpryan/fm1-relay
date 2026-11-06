/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* RELAY-16: a sequencer-first firmware for the M-VAVE FM-1 and SMK-37 on fm1-base. It makes no sound: sixteen tracks
 * play other instruments over USB (a computer, or a synth through the USB host), Bluetooth and, on the SMK-37, the
 * TRS jack. This header is the app's half of the contract with the platform (base/device/unity.c,
 * base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "RELAY-16"
#define APP_SPLASH_RGB RGB(95, 209, 192)
#define APP_BLOCK 256u                   /* frames per audio block (the render is silence) */
#define APP_CANVAS_PX (240u * 132u)      /* the tallest band, the page, is 240 x 132 */
#define APP_TICK                         /* the sequencer's clock: app_tick, 1 kHz in the timer interrupt */
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h): GLOBAL > PORTS */
#define APP_USB_HOST_NAME                /* the USB host's device by name (GLOBAL > PORTS) */
#define APP_BLE_AT_BOOT (!relay_ble_off())
int relay_ble_off(void);

/* the store: the settings (the platform's Bluetooth record and the panel calibration among them). The project itself
 * is not a store object: it is written whole into the data area (store.c), below these */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {PLAT_GLOB, 0xDC000u, 0xDE000u}
/* the project's flash: from the data area's start up to the store's first object at 0xDC000 */
#define RELAY_PROJ_LO PLAT_DATA_LO
#define RELAY_PROJ_HI 0xDC000u

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_tick(void);

/* the host simulator's checks (the tests' "expect" lines) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
