# Battery cycle #2 (2026-11-20/21) - analysis

Log: `battlog-20261121-093251.csv` (22331 records, 0 write errors). Plot: `cycle2.png`.

## What happened (reconstructed from flags; logger time t_s is NOT wall-clock across power-offs)

| Records | t_s | State | Note |
| --- | --- | --- | --- |
| 0-226 | 0-870 | USB, charging (top-off) | start 4193 mV |
| 227-3130 | 875-10970 | USB, charge done, S12 load | user away; 2.8 h on USB |
| 3131-4815 | 10975-16819 | battery, S12 | laptop battery died (VBUS lost) -> discharge 4168 -> ~3975 mV, 1.6 h |
| 4820-4863 | - | 44 empty slots | resume landed on a sector boundary (stale-record bug) |
| 4864-9388 | 18690-33547 | battery, S12, resumed | 3732 mV -> 3.30 V cutoff, 4.13 h. Board had been off/unknown in between: the middle of the curve (3.97 -> 3.73 V) is missing |
| 9389-9690 | 50519-53499 | USB charging | 50 min charge, 3290 -> 3733 mV, then USB removed |
| 9691-20978 | 53507-93573 | battery, S15 GAME MIX (resume used default ls=15 - bug) | 3998 mV (loaded) -> cutoff, 11.1 h |
| 20979-22330 | 93607-107085 | USB charging from empty | full charge curve |

## Results

- Discharge under S12 confirms cycle #1: the last 4 h (3.73 V -> cutoff) take 4.13 h vs 4.30 h in cycle #1 (-4 %),
  the first hour from full overlays cycle #1 within ~10-20 mV. Cycle #1's 9 h 18 min stands (not re-measured end
  to end because of the gap).
- Full charge from empty (3.30 V loaded cutoff, A-C cable): CC (~175 mA, HWCAL i_chg) until 4.20 V at ~143 min,
  CV until CHG release at 194.7 min (3 h 15 min total).
- Charge in (PRELIMINARY): 175 mA x 143 min = 417 mAh in CC + CV taper ~50-60 mAh (exponential 175 -> ~17.5 mA
  termination over 52 min) = ~470 mAh. Consistent with cycle #1's ~500 mAh discharge estimate -> F13: the cell holds
  ~1.5x its 310 mAh label. Needs one direct current reading in CC and late CV to firm up.
- S15 game mix: 11.1 h from 3.998 V loaded (~70-75 % SoC estimated) to cutoff -> roughly 14-16 h from full
  (extrapolated, preliminary).

## Logger bugs found and fixed (F16)

1. `resume` used default parameters (ls=15 game mix) instead of the original run's (ls=12). Fixed: run parameters
   stored in a BCFG block at 0xF00200 and restored on resume.
2. `new=1` did not wipe the record area; stale records beyond the write position fooled flash_count() on resume.
   Fixed: new start erases every non-blank 4 KB sector of the record area (88 sectors, ~seconds).
3. t_s after a power-off resume continues from the last record (no RTC across power-off) - documented, not fixable
   without a backup domain supply.
