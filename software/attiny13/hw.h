/* SPDX-License-Identifier: MIT */

#ifndef HW_H
#define HW_H

#include <stdint.h>

void hw_init(void);

uint8_t hw_pwr_raw_released(void);
uint8_t hw_pwr_raw_pressed(void);

uint8_t hw_off_ack_raw_active(void);

void hw_en_on(void);
void hw_en_off(void);

void hw_off_req_active(void);
void hw_off_req_inactive(void);

void hw_sleep_until_pwr_change(void);

#endif
