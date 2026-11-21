# Independent Review: STM-boy v0.1 Hardware Characterisation (2026-11-21)

Reviewer: independent agent (read-only). Scope: `docs/hardware-reference-v0.1.md`, `FINDINGS.md`,
`HW_LOG.md` (grepped), `tools/results/002800343435511331323538/`, the drivers that the game firmware will
reuse, `software/attiny13`, the BOM and the schematic text, and `software/stm32-core` plus
`stm32-retroport` from the main checkout. No hardware was touched. Local actions only: `make -s` (this
rebuilt `build/`, which is gitignored), `objdump` of the ELF, and Python over the CSV/JSON results
(scripts in `%TEMP%\rev`).

Tags: **VERIFIED** means I checked it in code, disassembly or raw data. **SUSPECTED** means it is reasoned
from datasheets or code but not checked on the board.

The work is thorough and mostly well evidenced. The points below are the gaps that will cost the most time
once game firmware starts.

## (a) Executive Summary: Top 10 By Impact On Game Firmware

1. **A likely root cause for F4 (silently ignored W25Q erases) is in the driver.** In `w25q_erase_cmd()`
   and `w25q_program_page_timed()` the CS-high pulse between WREN and the erase/program opcode is two
   back-to-back `str` instructions to `GPIOA->BSRR` (about 10-20 ns at 96 MHz). The W25Q128JV (BOM: U3
   W25Q128JVPIQ) needs tSHSL of at least 50 ns before write commands. `w25q_program()` has only one extra
   `ldr` in that gap. Symptoms match a WEL-set-too-late model: SR1 = 02 after a failure; on the first media
   upload all 203 64 KB erases failed (2 ms total); a retry, which runs with WEL already set, always worked.
   RetroPort's HAL-based driver has a much longer gap. VERIFIED (disassembly); the cause is SUSPECTED. Fix:
   a deselect delay of 100 ns or more in `end()`, then count retries.
2. **Save and persistence are not production-ready.** The gauge persistence that section 11 step 7 says is
   implemented does not exist: `hwcal_save()` is called only from `cmd.c`. The planned design puts
   frequently changing runtime state (gauge level and mode) in the same 4 KB sector as the one-off board
   calibration. Every second save erases that sector, leaving a 50 ms window in which a power loss wipes the
   calibration. A failed save calls `hwcal_load()`, which resets the RAM calibration to defaults. Plug and
   unplug events, which cause mode changes and therefore saves, are exactly when power can fail. Neither
   game-save storage nor a W25Q area for it is defined. VERIFIED.
3. **Low-battery handling and brown-out are not covered.**
   - Nothing acts on `GAUGE_F_CRITICAL` outside the battlog suite.
   - The documented rule "after cutoff, enter Stop" runs at 1.3 mA. That drains the charge left below the
     3.30 V cutoff in about a day and then trips the cell protection.
   - Standby mode was never tested.
   - BOR is off, while the W25Q needs 2.7 V or more.
   - A false VBUS reading disables all low-battery alarms (`reset_alarms()` while `vbus`). VBUS idles at
     about 1.1 V, right at the F411 VIL(max) of about 1.12 V.
   VERIFIED (code and arithmetic).
4. **Runtime figures overstate game battery life.**
   - "S12 9 h 18 min" includes 1.77 h of rests. Continuous maximum load is about 7.85 h (1876 mWh / 239 mW).
   - "S15 11.1 h" includes 2.16 h of rests. Its starting SoC is 82 % by the OCV table, not 70-75 %.
   - At the stated 124 mW, the S15 run implies only about 396 mAh, against 491-501 mAh from both S12 runs.
     The S15 power or the capacity is off by about 25 %.
   - S15 contains **no audio**, and the amplifier current was never metered.
   VERIFIED (battlog CSVs).
5. **The CPU budget leaves out real audio cost.** Section 12 budgets 2.9 % for audio, which is a mixing
   kernel figure. Measured engine costs are 7.8-10.9 % for engines A and B with 4 voices, and 27.4 % for NS.
   With NS, "left for the game" at 60 fps drops from 88.8 % to about 62 %. NS also cannot run at the
   48-84 MHz "scale down" clocks: at 48 MHz the 8-bit carrier becomes 187.5 kHz and its cost doubles to
   about 55 % CPU. VERIFIED (JSON and arithmetic).
6. **The W25Q driver does not wait for BUSY at init.** After a warm reset (IWDG, fault, DFU or
   `NVIC_SystemReset`) during an erase, which can take up to 400 ms for 4 KB or 2 s for 64 KB, the JEDEC
   read is ignored by the busy chip. `s_present` becomes false, HWCAL falls back to defaults and the game
   data is "missing" for that boot. Once game firmware uses a watchdog this is a realistic path. VERIFIED
   (code).
7. **Every press is delayed by 20 ms.** `input_tick_1ms()` applies one symmetric stability window, so the
   press is reported only after 20 ms of stable level, not just the release. The latency estimate in the
   doc leaves this out. End-to-end latency was never measured. A spurious B press seen on 2026-11-19 was
   masked ("auto mode now ignores keys") rather than explained. VERIFIED.
8. **The platform contract and the engine disagree.** `stm32-core/engine/platform.h` uses 128x160
   (portrait), 7 buttons including `BTN_START` with different bit values, and `plat_millis()`. The
   reference proposes 160x128 landscape and a 10-bit mask without START. There is no Start/Select: POWER is
   not a usable button. VERIFIED.
9. **The flash map is inconsistent.** The media container at `0x200000-0xEFFFFF` (`W25Q_MEDIA_BASE`) lies
   inside the 14 MiB the layout table reserves for the RetroPort ROM pack. The table omits the media region
   and has no save-game region. Sections 5 and 10 still recommend "the 64 KB erase where possible", although
   the 64 KB erase failed 100 % during the media upload. VERIFIED.
10. **Crash breadcrumbs cannot be trusted.** `info.faults` read 536879233 and then 536879624
    (`0x20002081`, `0x20002208`) from the 2026-11-18 overclock sweep until a cold boot. The fault PCs
    `0x28002190` and `0x28002186` look like bit-29 flips of `0x0800xxxx`. The `.noinit` breadcrumbs have no
    checksum, and the fault handler writes them at the faulting (unstable) clock. Nobody noticed it in
    HW_LOG. VERIFIED (data); cause SUSPECTED.

## (b) Findings Table

Severity: C = critical, I = important, m = minor. Effort: S = under 1 h, M = half a day, L = 1 day or more.

| ID | Area | Sev | Tag | What | Why it matters for game firmware | Action (effort) |
| --- | --- | --- | --- | --- | --- | --- |
| R1 | W25Q / F4 | C | VERIFIED code, SUSPECTED cause | CS-high pulse WREN -> opcode is 1-2 HCLK (`str r2,[r3,#24]` x2 at `0x802a458`; same in `w25q_program_page_timed`; `w25q_program` has +1 `ldr`). JV tSHSL is 50 ns or more. All observed symptoms fit (SR1 = 02, first try fails and retry works, 100 % failure in the hot media loop) | Saves rely on erase and program; a marginal program would fail silently too; the retry hides the cause and depends on DWT->CYCCNT being enabled (`timebase_init`) | Add a deselect delay of 100 ns or more after `CS_HIGH()` (read back GPIOA->IDR plus about 8 NOPs, or `delay_cycles(12)`). Count retries over 1000 sector erases and 1000 page programs before and after. Keep verify-after-write (S) |
| R2 | Persistence | C | VERIFIED | Ref section 11 steps 7-8 (persist level/mode, low-battery save + Stop) are not implemented; `hwcal_save` is only reachable via the command line | The reference reads as "done"; a game developer would assume it | Mark as DESIGN, not implemented, in the doc (S) |
| R3 | HWCAL design | C | VERIFIED | Calibration and runtime gauge state share one sector; the erase window is about 50 ms; a failure path (`program_record` fails -> `hwcal_load()`) sets `g_hwcal` to defaults, and the next save persists the defaults | One unlucky unplug with a flat cell loses the board calibration permanently | Split: (1) calibration sector, write-once, two copies in two sectors; (2) runtime and settings as a log-structured CRC + seq record ring over 2 or more sectors (like RetroPort `RFS1`). Never call `hwcal_load()` into the live struct on failure (M) |
| R4 | Save game | C | VERIFIED (absent) | No save-game design: no region, no wear plan, no power-loss rules | First thing every game needs | Define a 64 KB+ save region and a record format (seq, CRC, two slots, magic last, as HWCAL does). Budget writes to under 2 s after OFF_REQ (ATtiny timeout 2.5 s, RC +-10 %). Never start a 64 KB erase after OFF_REQ (M) |
| R5 | W25Q init | I | VERIFIED | `w25q_init()` reads JEDEC immediately and does not poll BUSY; during an erase the chip ignores 9Fh | Warm reset in an erase -> "no flash", HWCAL defaults, saves invisible for that boot | In init: release PD, then poll SR1 BUSY up to 3 s before JEDEC; retry JEDEC 3x (S) |
| R6 | Brown-out | C | SUSPECTED | BOR off (OPTCR `0x0FFFAAED`). TPS63802 holds 3V3 until the cell protection or the ATtiny cuts, then the rail decays through 2.7 V (W25Q min) with the MCU still running down to about 1.8 V | Writes during rail decay can corrupt flash | Program BOR level 3 once. This is **possible from the application via FLASH->OPTCR** (doc says "needs SWD"); do it as a guarded first-boot step and verify by reading back (S) |
| R7 | Low battery | C | VERIFIED (arith.) | After cutoff the doc rule is "enter Stop". Stop S1 = 1.3 mA; the reserve below the 3.30 V loaded cutoff is about 5 % (about 25 mAh) -> under 1 day to protection cutoff. Standby (about 2-3 uA MCU) never tested | Deep-discharging the LiPo repeatedly damages it; the user leaves a dead console in a drawer | Test Standby + RTC wake (every 5-10 min check VBUS/CHG) and measure the board Standby current; decompose the 1.3 mA Stop current (DBGMCU_CR DBG_STOP left by ST-Link? divider, LCD) (M) |
| R8 | VBUS sense | I | VERIFIED (doc value vs DS) | Idle PA9 about 1.1 V with the pull-down vs VIL(max) = 0.35 VDD - 0.04 = 1.115 V | Another board or temperature -> false VBUS -> gauge ignores load, `reset_alarms()` -> no low-battery cutoff on battery | Cross-check VBUS with CHG and USB SOF/suspend state; treat "VBUS but no USB activity and CHG high for 10 s" as suspect; v0.2 external 47k (already listed) (S) |
| R9 | Runtime | I | VERIFIED | S12 9:18 = 7.54 h load + 1.77 h rest; continuous max load about 7.85 h | Battery-life claims for games | Doc: state rests explicitly; add the continuous figure (S) |
| R10 | Runtime / capacity | I | VERIFIED | Cycle #2 S15: first end-of-rest 4001 mV -> SoC 82.3 % (doc 70-75 %); 8.95 h load + 2.16 h rest; at 124/41.3 mW delivered 326 mAh -> implied Q 396 mAh vs 491 (cycle #2 S12) and 501 (cycle #1). The S15 power was metered on 2026-11-16 with an older build | "14-16 h from full" is not supported; F13 rests on one metered S12 point and the nominal ISET (KISET +-10 %) | Meter S15 and S12 once more mid-discharge (about 3.7 V) on the current build. Measure the CC charge current on the **2 A range** (burden on 200 mA x 175 mA would push the charger into CV early). Re-run the analysis (M) |
| R11 | Audio power | I | VERIFIED | S15 "game mix" = BL 50 % + 30 fps + 50 % CPU + W25Q reads, **no audio**; S10/S11 never metered (F6 meter artefact) | Amp quiescent + speaker + NS 27 % CPU are missing from every runtime estimate | Measure S10, S11 and "S15 + NS music at game volume" on the 2 A range, or via a 0.1-0.5 ohm shunt with the scope (S) |
| R12 | CPU budget | I | VERIFIED | Section 12 per-frame table uses `audio_4ch_22k_pct` 2.92 (mixing kernel) vs `audio.cpu` 7.8-10.9 % and NS 27.4 % | Game designers will plan with 88.8 % free | Replace with the engine numbers; show NS and B variants (S) |
| R13 | DFS vs audio | I | VERIFIED (arith.) | Recommended 48-84 MHz "menus/pause" clocks change the TIM1 clock: NS 375 kHz/8-bit is impossible at 48 MHz; cost at 70 cyc/period grows to about 55 % | Clock scaling and the audio engine are coupled | Doc rule: audio engine/Fs fixed per clock profile (96 or 144 only for NS); stop audio or switch to engine B before scaling (S) |
| R14 | Input latency | I | VERIFIED | Press reported after 20 ms stable (same window as release). HWCAL `debounce_ms` = 20 | +20 ms on every press; the doc estimate (20-40 ms) omits it | Eager press (report the first edge, then lock out 20 ms), stability window only for release (S). Measure end to end with a 240 fps phone video (the suite prompt already supports it) (S) |
| R15 | Input | m | VERIFIED (log) | Unexplained spurious B edge (HW_LOG 2026-11-19 media trial); B has 17.6 ms release bounce | Phantom presses in games | Run `input.bounce` / edge log idle for 30 min with audio + backlight + USB on; check whether B edges correlate with amp or BL activity (S) |
| R16 | Platform API | I | VERIFIED | `stm32-core/engine/platform.h`: SCREEN 128x160, BTN_START, bits differ, `plat_millis` | Two incompatible contracts | Pick one now: landscape 160x128, 10-bit mask; map "Start" to a chord or CENTER; add `plat_millis`, save API (S) |
| R17 | Flash map | I | VERIFIED | Media `0x200000-0xEFFFFF` overlaps RetroPort-reserved space; table lacks it; no save region | Cross-firmware data loss | One authoritative map in section 5 (ROM pack, media, saves, settings, HWCAL, test scratch) (S) |
| R18 | Doc F4 advice | I | VERIFIED | Sections 5 and 10 say "use the 64 KB erase where possible"; FINDINGS F4 shows 64 KB failed 100 % on the first upload | Misleading mitigation | Rewrite after R1 (S) |
| R19 | .noinit faults | I | VERIFIED data | `fault_count` held RAM-like garbage from 2026-11-18 21:09 to 2026-11-19 12:03; PC 0x2800219x | Crash reporting in games will lie | Add a CRC/magic per breadcrumb set; in the fault handler switch SYSCLK to HSI before writing; clamp/validate on read (S) |
| R20 | IWDG + Stop | I | SUSPECTED (RM0383) | F411 has no IWDG freeze-in-Stop option; IWDG max about 32 s at LSI | A game with IWDG resets out of Stop/suspend | RTC wake under the IWDG period to kick it, or no IWDG in suspend builds; test once (S) |
| R21 | Stop wake | m | VERIFIED | `lp_stop()` calls `clock_init_96mhz()` before `HAL_ResumeTick()`; HAL HSE/PLL timeouts use `HAL_GetTick()` -> no timeout if HSE fails after wake | Hang instead of HSI fallback | Resume the tick first, or use bounded register loops (S) |
| R22 | Gauge config | m | VERIFIED | `capacity_mah` 310 (not in HWCAL) drives charge coulomb counting -> level rises about 1.5x too fast (capped at 4/5); load model uses backlight state only (S2-S7), not CPU/LCD/audio; RINT default 150 vs HWCAL 330 | Wrong charge icon; small SoC bias under game load | Add capacity to the calibration; feed the real platform load state (S) |
| R23 | R_int value | m | VERIFIED | Doc 291/376 mOhm came from the 207 mW default; the analysis with the measured 239 mW gives **244/315 mOhm** (`-analysis-p12meas/summary.md`) | Gauge load compensation | Doc fix (S) |
| R24 | Amp pop path | m | VERIFIED code | `audio_amp_on(0)` waits 30 ms only if `CCR1 != mid`. After `audio_pwm_start(c, true)`, CCR1 is already mid -> SD# rises immediately while C25 is still charging (tau about 13 ms) | Possible pop at first enable after boot | Track the time since PWM start; always wait 30 ms from the first mid-scale output (S) |
| R25 | Audio buffers | m | VERIFIED | Engine B/NS DMA ring and NS state default to `g_arena` (test arena) | Game must supply `audio_set_buffer()`; easy to miss | Make the buffer a required init parameter (S) |
| R26 | Hiss method | m | VERIFIED (absent) | Backlight level and USB/battery not recorded in `hiss.md`; resolution about +-3 dB (hi-Z read -2.9 dB below "amp off"); BL PWM is 1 kHz = in the speaker band | 3V3 floor may partly be backlight ripple | Repeat H-b2/NS idle with BL off/50/100 % and BL PWM 1 kHz vs 25 kHz (S) |
| R27 | LCD robustness | m | VERIFIED code | `lcd_sleep(true)` waits 5 ms (ST7735S needs 120 ms after SLPIN before SLPOUT or power removal); no readback/TE, so a panel reset by ESD cannot be detected; power-off cuts the rail without SLPIN | Glitches on suspend/resume; white screen after ESD | Enforce 120 ms; periodically (or on resume) re-send MADCTL/COLMOD/DISPON; SLPIN before ACK (S) |
| R28 | Boot time | m | VERIFIED | HW reset + SWRESET (150 ms) + SLPOUT 255 ms + DISPON 100 ms; boot to first pixel 758 ms | Game boot feel | Drop SWRESET after HW reset; SLPOUT 120 ms (S) |
| R29 | ROM bootloader pins | m | SUSPECTED (AN2606) | F411 bootloader also probes USART2 (PA2/PA3 = OFF_REQ/ACK), I2C1 (PB6/PB7 = RIGHT/LEFT), SPI2 (PB12-15 = X/A/B/Y), I2C3 (PA8/PB4) | Doc warns only about CENTER | "Press no button in the ROM bootloader"; note POWER/OFF_REQ activity (S) |
| R30 | 1200-baud touch | m | VERIFIED (design) | Any host opening the CDC at 1200 baud resets into DFU | Random resets in a shipped game | Keep in dev builds only, or require a command + confirmation (S) |
| R31 | ATtiny | m | VERIFIED | HFUSE 0xFF = ATtiny BOD disabled; RC clock +-10 % (all timeouts); `config.h` comment still says "Prescaler 64"; hard reset (8 s) cuts without any notice to the MCU; post-F1 timings not scope-measured | Save budget after OFF_REQ is really about 2.2 s; brown-out behaviour of the ATtiny undefined | Scope OFF_REQ -> rail drop without ACK once; consider BODLEVEL 1.8 V (S) |
| R32 | Charger | m | SUSPECTED (schematic text) | BQ24075 TS appears tied to a fixed 10k (R8): no battery temperature qualification; charging while playing in a warm case untested (die temp only) | Thermal safety | Log battery/case temperature (thermocouple or NTC on the cell) during charge + S12 in the closed case (M) |
| R33 | Clock/RTC | m | VERIFIED | No LSE fitted; RTC on uncalibrated LSI (17-47 kHz spec); no backup supply -> no wall clock across power-off | Timers in Stop inaccurate; no real-time features | Measure LSI via TIM5/TIM11 capture once; document "no RTC" (S) |
| R34 | Stack | m | VERIFIED (absent) | No stack painting/watermark; 4 KB min stack with nested ISRs (SysTick, LCD, audio, USB, EXTI) | Silent corruption in bigger games | Paint the stack and report the high-water mark in `info` (S) |
| R35 | HWCAL compat | m | VERIFIED | `read_half()` rejects `len > sizeof(hwcal_t)` and any version != 1 -> FOREIGN -> saves refused | hwtest and game firmware with different record versions lock each other out | Accept a longer len (read the known prefix, CRC over full len) (S) |
| R36 | DMA2 errata | m | SUSPECTED (ES0287) | DMA2 concurrent AHB-peripheral + APB2 access errata; the current mapping (SPI1, TIM1, ADC, M2M) is safe | Future GPIO-DMA tricks on DMA2 would corrupt data | Doc rule: no DMA2 to GPIO/AHB peripherals (S) |
| R37 | Revisions | m | VERIFIED | `revisions.md` still says "RetroPort power-off root cause not identified"; F1 (ATtiny 8x timing) very likely explains it | Stale known issue | Retest RetroPort power-off with the fixed ATtiny; update (S) |
| R38 | Joystick | m | VERIFIED | Diagonals tested: UP+RIGHT, DOWN+LEFT only | Low risk (direct GPIO, no matrix) | Test UP+LEFT, DOWN+RIGHT once (S) |

## (c) Solid: Trust These As They Are

- Pin map, EXTI conflicts, PB3/PB4/PA15 JTAG traps, `pins` suite methodology (43 of 44 checks).
- Clock tree at 96 MHz, flash WS/ART findings, the overclock table and the advice "96 MHz is the graphics
  optimum; do not ship more than 100 MHz".
- Display recipe: offsets, native scan order (MADCTL 0x80) against tearing, init sequence, GDDRAM clear,
  48 MHz SPI with GPIO speed high, DMA present timing (6.83 ms), the 8bpp-palette pipeline and COLMOD
  numbers.
- DMA/IRQ allocation and block C stress: 0 underruns, 0 DMA/CRC errors with LCD + audio + W25Q + ADC
  together.
- W25Q read path (DMA 2.99 MB/s, whole-chip CRC at 6/12/24 MHz) and the write guard.
- Backlight PWM (scope-confirmed) and per-level currents; the "backlight dominates" conclusion.
- DFU entry/exit via `.noinit` magic (20/20) and recovery paths, including SWD connect-under-reset.
- Pop-free amplifier order (precharge, then SD#), PA8 float for silence, speaker protection limits, in-case
  frequency response and EQ. The hiss decomposition is convincing in direction (NS about 15 dB below
  engine B).
- OCV table shape and the S12 discharge (cycle #2 confirms cycle #1 within about 4 %); ADC gain/VREFINT
  calibration.
- Power-off handshake logic (MCU side) and the F1 root cause/fix.

## (d) Proposed Minimal Next Test Session (about 5 h)

Equipment: multimeter (use the **2 A range** for currents above 100 mA or with the charger; or a
0.1-0.5 ohm shunt read with the OWON), oscilloscope, phone at 240 fps, a USB-A-to-C cable, and optionally an
NTC or thermocouple. Order matters: firmware fixes first, so later measurements use the fixed driver.

| # | Step | Time | Equipment |
| --- | --- | --- | --- |
| 1 | R1: add the CS deselect delay; add retry counters to erase and program; run 1000 x 4 KB erase + program + verify at 0xF00000 and 100 x 64 KB at the media area, before and after the fix. Expect retries to drop from >0 to 0 | 45 min | none |
| 2 | R5: BUSY poll in `w25q_init()`; test with `reset` issued during a 64 KB erase (a command that starts the erase and resets after 50 ms) | 20 min | none |
| 3 | R14: eager-press debounce; then `input.latency` with the 240 fps phone video (screen + button in frame), 20 presses | 30 min | phone |
| 4 | R11/R10: meter S10, S11, S12 and S15 on the current build at about 3.7 V; add "S15 + NS music" (game volume) | 45 min | meter (2 A range) or shunt + scope |
| 5 | R10/F13: CC charge current on the 2 A range at 3.5 V and 4.0 V, plus one late-CV point | 20 min | meter |
| 6 | R7: Standby + RTC wake prototype; measure board current in Stop (with and without SWD ever attached since POR, to check DBGMCU) and in Standby | 45 min | meter (2 mA / 200 uA range) |
| 7 | R31: scope OFF_REQ -> 3V3 drop without ACK (timeout), and hold time to OFF_REQ, after the F1 fix | 15 min | scope |
| 8 | R26/R15: idle hiss with BL off/50/100 % and BL PWM 1 kHz vs 25 kHz; 30 min idle edge log with amp + BL + USB active | 40 min | phone, quiet room |
| 9 | R6: program BOR level 3 from firmware (guarded), read OPTCR back, confirm boot | 15 min | none (SWD as a safety net) |
| 10 | R38/R27: remaining diagonals; suspend/resume x 50 with 120 ms SLPIN wait, check for panel glitches | 15 min | none |

After the session, update in the doc:
- the runtime figures (R9, R10) and the CPU budget (R12);
- R_int (R23) and the flash map (R17, R18);
- mark gauge persistence and low-battery handling as "design, not implemented" (R2);
- add a short "save-game and power-loss rules" subsection (R3, R4, R6) and the DFS/audio rule (R13).
