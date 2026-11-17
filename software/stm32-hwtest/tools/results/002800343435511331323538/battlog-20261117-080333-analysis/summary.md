# Battery log analysis: battlog-20261117-080333.bin

- records: 9702, log span 14:52:42, Vbat 3290..4190 mV, T 20.9..35.0 C
- discharge duration (USB unplug .. cutoff): 9:18:32
- Stop gap (cutoff .. USB plug): 4:42:52, drain 6.13 mAh at 1.3 mA
- charge duration (plug .. CHG high): n/a (CC n/a, CV n/a)
- delivered charge (to cutoff): 436.2 mAh; energy 1634 mWh; average power 175.6 mW
- charge-side: CC n/a + CV tail n/a = n/a mAh (eta applied -> n/a % of delivered after Stop-drain correction)
- S12 power: **207.0 mW** (default estimate)
- Q_total used for the SoC axis: 436.2 mAh (nominal 310)
- R_int from load<->rest steps: +1 s median 290.8 mOhm (n=100), +10 s median 375.5 mOhm (n=100)
- charger plug step at t=50519 s: V 3290 -> 3428 mV (+1 s), R simple 773.6 mOhm, refined 568.8 mOhm
- charger unplug step at t=53507 s: V 3726 -> 3688 mV (+1 s), R simple n/a mOhm, refined n/a mOhm (charger in CV/terminated: current unknown, no R_int)

## R_int vs SoC (+1 s step, median per 10 % bin)

| SoC | R_int mOhm | n |
|---|---|---|
| 0-10% | 389.7 | 10 |
| 10-20% | 392.7 | 10 |
| 20-30% | 259.0 | 10 |
| 30-40% | 153.0 | 10 |
| 40-50% | 300.9 | 8 |
| 50-60% | 225.9 | 10 |
| 60-70% | 333.3 | 10 |
| 70-80% | 71.4 | 12 |
| 80-90% | 182.2 | 10 |
| 90-100% | 565.6 | 10 |

## OCV table (0..100 % SoC, step 5 %)

| SoC % | OCV mV |
|---|---|
| 0 | 3320 |
| 5 | 3475 |
| 10 | 3505 |
| 15 | 3541 |
| 20 | 3568 |
| 25 | 3589 |
| 30 | 3614 |
| 35 | 3647 |
| 40 | 3667 |
| 45 | 3688 |
| 50 | 3741 |
| 55 | 3781 |
| 60 | 3826 |
| 65 | 3865 |
| 70 | 3899 |
| 75 | 3954 |
| 80 | 3994 |
| 85 | 4009 |
| 90 | 4060 |
| 95 | 4110 |
| 100 | 4161 |

```c
static const uint16_t ocv_mv[21] = {
    3320, 3475, 3505, 3541, 3568, 3589, 3614,
    3647, 3667, 3688, 3741, 3781, 3826, 3865,
    3899, 3954, 3994, 4009, 4060, 4110, 4161,
};
```

HWCAL: `hwcal set` in Src/cmd.c only accepts display/backlight/debounce scalar fields (madctl_*, xoff_*, yoff_*, bgr, invert, gamma_id, spi1_*, debounce_ms, bl_pwm_hz, bl_min_level). `ocv_mv[21]`, `rint_mohm` and `i_chg_ma` can NOT be set from the host yet; put the array above into `ocv_default` in Src/hwcal.c (or add the keys to hwcal_set_field), then `hwcal set rint_mohm=376` once supported.
Suggested values: rint_mohm=376, i_chg_ma=175 (ISET 5.1k, 174.5 mA nominal).

## Runtime estimates (OCV table, constant power, R_int; no extrapolation below the S12 cutoff)

| load | mW | hours |
|---|---|---|
| game mix (S15) | 124 | 13.2 |
| idle menu BL100+busy (est) | 200 | 8.2 |
| BL50 static (S5) | 99 | 16.5 |
| Stop | 5 | 328.4 |

## Assumptions / caveats

- constant-power load states (buck-boost), I = P/Vbat; powers from the HW_LOG table, S12 solved from the charge side.
- interval state = state of the later record; time resolution is 1 s.
- SoC 100 % = start of discharge (assumed full), SoC 0 % = loaded cutoff 3.30 V (not empty).
- charge efficiency eta = 1.0 (coulombic); CC current 174.5 mA; CV tail modelled as exponential to 10 % ISET.
- short rests (1 min) are not fully relaxed: they bias OCV low by a few mV; 15-min rests weigh 10x.
- R_int(step) includes some fast polarisation (+1 s) / more (+10 s); plug-step 'refined' value divides by (I_chg + I_load) of the last loaded sample (upper bound, polarisation of the loaded sample leaks in).
- NOTE: charge phase did not reach CHG-high: no charge-side capacity cross-check
- NOTE: no cutoff + complete charge: S12 power not derivable, default estimate 207 mW used
