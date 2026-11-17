# Battery log analysis: battlog-20261113-214159.csv

- records: 90, log span 0:01:57, Vbat 4157..4221 mV, T 26.5..30.0 C
- all records on USB power: dry run or no battery discharge, nothing derived

## Assumptions / caveats

- constant-power load states (buck-boost), I = P/Vbat; powers from the HW_LOG table, S12 solved from the charge side.
- interval state = state of the later record; time resolution is 1 s.
- SoC 100 % = start of discharge (assumed full), SoC 0 % = loaded cutoff 3.30 V (not empty).
- charge efficiency eta = 1.0 (coulombic); CC current 174.5 mA; CV tail modelled as exponential to 10 % ISET.
- short rests (1 min) are not fully relaxed: they bias OCV low by a few mV; 15-min rests weigh 10x.
- R_int(step) includes some fast polarisation (+1 s) / more (+10 s); plug-step 'refined' value divides by (I_chg + I_load) of the last loaded sample (upper bound, polarisation of the loaded sample leaks in).
- NOTE: all records have VBUS=1 (dry run or USB-powered): the cell is not discharging, discharge / OCV / R_int analysis skipped
