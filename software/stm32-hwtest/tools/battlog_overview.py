"""One overview figure of the battery cycle: Vbat, die temperature, VDDA, load state over clock time."""
import csv
import sys
from datetime import datetime, timedelta

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

src, out = sys.argv[1], sys.argv[2]
start = datetime(2026, 11, 16, 16, 58)  # logger start (HW_LOG), approx.
rows = list(csv.DictReader(open(src, encoding="utf-8")))
t = [start + timedelta(seconds=int(r["t_s"])) for r in rows]
v = [int(r["vbat_mv"]) / 1000 for r in rows]
temp = [float(r["temp_c"]) for r in rows]
vdda = [int(r["vdda_mv"]) / 1000 for r in rows]
st = [int(r["load_state"]) for r in rows]
vbus = [int(r["vbus"]) for r in rows]

fig, ax = plt.subplots(4, 1, figsize=(13, 11), sharex=True,
                       gridspec_kw={"height_ratios": [3, 2, 1.3, 1]})
ax[0].plot(t, v, lw=0.6, color="tab:blue")
ax[0].set_ylabel("Vbat, V")
ax[0].set_title("STM-boy battery cycle 2026-11-16/17 (load S12 max 9 min / rest 1 min, then Stop, then charge)")
ax[0].axhline(3.30, color="tab:red", ls="--", lw=0.8)
ax[0].text(t[0], 3.31, "cutoff 3.30 V (loaded)", color="tab:red", fontsize=8)
ax[1].plot(t, temp, lw=0.6, color="tab:orange")
ax[1].set_ylabel("MCU die temp, °C")
ax[2].plot(t, vdda, lw=0.6, color="tab:green")
ax[2].set_ylabel("3V3 (VDDA), V")
ax[3].step(t, st, where="post", lw=0.6, color="tab:gray", label="load state")
ax[3].step(t, [s * 12 for s in vbus], where="post", lw=1.0, color="tab:purple", label="USB (VBUS)")
ax[3].set_ylabel("state")
ax[3].set_yticks([0, 2, 12])
ax[3].legend(loc="upper left", fontsize=8)
for a in ax:
    a.grid(alpha=0.3)
ax[3].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
ax[3].xaxis.set_major_locator(mdates.HourLocator(interval=1))
fig.tight_layout()
fig.savefig(out, dpi=110)
print(out)
