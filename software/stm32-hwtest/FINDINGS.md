# STM-boy v0.1 Findings and Errata (working list)

Running list of hardware/firmware findings from `stm32-hwtest`. Source for
`docs/hardware-reference-v0.1.md` (errata, recommendations). Details and raw
evidence are in `HW_LOG.md` and `tools/results/`.

## Open Issues (need follow-up)

| ID | Finding | Evidence | Next step |
| --- | --- | --- | --- |
| F1 | CLOSED 2026-11-18 (reflashed, user verified POWER works correctly). ATtiny13 power-off hold time is ~4 s instead of 0.5 s (2-3 s hold + release does nothing). ROOT CAUSE: `attiny13/timebase.c` started Timer0 with /64 while OCR0A was computed for /8 (config.h) - fixed in code (e1aa2f5) and REFLASHED by the user 2026-11-17, verified 2026-11-18 -> all ATtiny intervals ~8x longer (ACK timeout ~20 s, hard reset ~64 s). May explain "RetroPort does not power off". | User observation 2026-11-13, power.handshake | Check `attiny13/config.h` / `timebase.c` math; measure OFF_REQ timing with scope; reflash ATtiny (needs avr-gcc + USBasp) |
| F2 | PB2 `AMP_SD` reads TIED_LOW, expected weak 100k pull-down (R23). Pull-down is much stronger than 100k (PAM8302A SD input or wrong R23). | `pins` suite, twice | Measure PB2 net to GND (board off) |
| F3 | CLOSED 2026-11-16: scope shows 1.000 kHz / 49.99 %% (firmware IDR sampling artefact). Backlight PWM duty measured 47.4 % for 50 % requested at every frequency (frequency exact). Likely IDR-sampling artefact. | `backlight` suite | Scope on PA6 / Q1 base |
| F4 | MITIGATED 2026-11-18: `w25q_erase_sector()` now goes through `w25q_erase_cmd` and retries when the erase "finishes" in < 1 ms (rejected command); 3 consecutive `hwcal save` incl. an erase passed. Root cause of the rejected 0x20 command still unknown. `hwcal save` erase of 0xFFF000 sometimes does not take effect (step=erase_verify, SR1=02 = WEL still set). Recurred 2026-11-17 twice in a row; a standalone `w25q erase4k FFF000 force` then works. 2026-11-18: 4 KB erase (0x20) at 0xFE0000 also silently rejected with WEL stuck; 64 KB block erase (0xD8) works -> 4K erase unreliable in the top region of the chip. | HW_LOG 2026-11-13, 2026-11-17 | Investigate hwcal_save erase path (WREN/ready sequence, SPI state left by other code) |
| F6 | Enabling the PAM8302A (AMP_SD high) browned out the ATtiny (powered from +V) when the battery path had extra series resistance (multimeter 200 mA range, ~7 ohm) -> ATtiny reset -> power cut. Risk with a weak/cold battery or high-impedance cell. | power.current S10, 2026-11-16 | Re-test amp enable on battery without meter at low SoC; consider amp soft-start / bulk cap on +V / ATtiny BOD |
| F7 | `lowpower.c` RTC wakeup flag clear wrote INIT=1 (stopped the RTC) -> Stop never woke by timer. FIXED in firmware. | power.current S1 | — |
| F8 | Board back-feeds ~1.9-2.5 V onto the +5V/VBUS rail (known float). With a USB-C to C cable the laptop (USB-C source) sees VBUS not at vSafe0V (<0.8 V) and never turns on 5 V -> no charging, VBUS sense false, while USB data still works. Wall charger (no vSafe0V check) charged fine. | +5V TP = 1.87-2.0 V with laptop C-C cable, 2026-11-17 | Confirm: +5V with cable removed; A-C cable test. v0.2: find the leak path / add VBUS bleed so +5V < 0.8 V without USB |
| F9 | CLOSED: the battlog DID record chg=1 (302 records, t=50519..53499); the analyzer just failed to detect the CC phase. Note: battlog t_s does not advance during Stop (time base excludes the 4:42 Stop gap). | re-check of battlog-20261117-080333.csv | fix battlog_analyze.py CC detection; add RTC time across Stop |
| F10 | Battery/VDDA/temperature ADC readings get much noisier during long low-load rests (S2, BL off): spikes on Vbat, VDDA and die temp together. Likely TPS63802 auto power-save (MODE=GND) -> burst-mode ripple on 3V3/VDDA; BAT_STAT node has no filter cap. | battlog-20261117-080333 overview.png (18:20, 20:30, 21:50-22:10) | Gauge: stronger averaging at low load; v0.2: cap on BAT_STAT, consider forced-PWM MODE or VDDA filtering |
| F11 | RESOLVED (recommendation): landscape MADCTL 0xA0 writes perpendicular to the native portrait scan -> diagonal tear; at 60 fps vs panel 60.25 Hz the seam drifts right->left every ~4 s (beat, unfixable without TE). Native-order transfer (MADCTL 0x80 = MY, MV=0, frame stored column-major / rotated 90 deg) makes the tear practically invisible at any fps/panel-rate combination (user). Remaining blur = TN pixel response. Rotation cost ~1.3 ms/frame (~2.2 ms incl. 8bpp->565). | display.tear, user 2026-11-17 | Engine: render/convert in native order, MADCTL 0x80 |
| F12 | Audio: analog chain (PAM8302A on 3V3) saturates at about -8 dBFS digital (1 kHz level stops rising from -8 to 0 dBFS); 200 Hz is inaudible below -10 dBFS and above that only distortion/rattle appears. During the clip test (0 dBFS, 200 Hz) the speaker got very hot. | audio.clip + phyphox 2026-11-19 | Limit digital output to -9 dBFS, high-pass ~400-500 Hz in the mixer, no sustained loud LF tones; measure speaker impedance/rating |
| F5 | Button B release bounce up to 17.6 ms (others < 4 ms). | `input.bounce` | Use >= 20 ms release debounce; check switch |

## Confirmed Facts

- Speaker (2026-11-19, iPhone mic at 10 cm, phyphox, fan background -55 dB): usable band ~800 Hz - 10 kHz
  (stepped sine, rel. to 1-8 kHz median: 800 Hz -16, 1 kHz -9, 1.25 kHz +3, 1.6 kHz +7, 2.5 kHz -13 (not
  confirmed by chirp), 3.15 kHz +6, 6-8 kHz -2, 10 kHz -5, 12.5 kHz -11 dB); below ~500 Hz under the fan noise
  (>= 25 dB down). Idle hiss at 2 cm hidden by the fan (all cases within +-1 dB; 94/187 kHz carriers +2/+1 dB);
  user hears hiss -> repeat in a quiet room with a spectrum.

- Block C (2026-11-18, 96 MHz): game-like load (raycaster 60 fps + 4-voice audio DMA + W25Q DMA
  streaming + ADC) for 60 s and a 3-min soak: 0 audio underruns, 0 DMA errors, 0 CRC errors, CPU ~31 %.
  DMA2 priorities: audio S5 very high, LCD S3 high; W25Q DMA1 S0 high. COLMOD full-frame DMA:
  12-bit 5.12 ms, 16-bit 6.83 ms, 18-bit 10.24 ms. Video streaming from W25Q: 8bpp 140 fps at
  SPI3 24 MHz (2.87 MB/s). LCD sleep-out 132 ms, Stop wake (HSE+PLL) ~0.9 ms, USB re-enum ~350 ms.
  Amp-enable dip on USB power ~14 mV (repeat on battery for F6).

- Display: landscape MADCTL 0xA0 offsets x=1 y=2; portrait 0x00 x=2 y=1;
  RGB order; inversion off; gamma table "sitronix" preferred. Bring-up values
  COLSTART=3/ROWSTART=1 were wrong (lost right column and top row).
- SPI1 to ST7735S stable at 48 MHz (8- and 16-bit frames) at SYSCLK 96 MHz; scope (PA5): 48.01 MHz,
  duty 48 %; 48 MHz needs GPIO speed >= medium (low fails: ~1 V swing); 12 MHz on low speed is a clean
  square (rise 11 ns). Edge ringing seen with fast speeds is mostly probe-loop; v0.2: 22-33 ohm series R on SCK/MOSI.
- Full-frame 160x128 RGB565 via DMA: 6.83 ms at 48 MHz (146 fps).
- DFU: software entry (command / 1200-baud touch) 20/20, BOOT0 path and
  recovery from a broken image verified. App->DFU ~0.61 s, DFU->app ~0.76 s.
- MCU: STM32F411 DEV_ID 0x431, Rev A, system bootloader 0xD0. Option bytes:
  RDP 0, no WRP, BOR off (1.8-2.1 V) - recommend BOR level for battery use.
- W25Q128: JEDEC EF4018, no block protection, 3 MB/s DMA read at 24 MHz.
  RetroPort uses 0x000000 (settings) and 0x100000-0x133670 (ROM pack).
- Battery ADC: divider 33k/82k accurate (gain 1.000 vs multimeter at 4.175 V);
  VDDA via VREFINT within 0.2 % of multimeter (3.293 vs 3.300 V).
- Power-off handshake works with USB attached (3V3 really drops right after
  ACK) when the ST-Link is disconnected.
