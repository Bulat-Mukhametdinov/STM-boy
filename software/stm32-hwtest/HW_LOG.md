# Hardware Work Log

Chronological log of every action performed on the physical STM-boy board
(serial `315A356C3435`) during `stm32-hwtest` development: what was done,
the exact command, and the result or error. Manual entries are written by
the developer/agent; entries tagged `[auto]` are appended by
`tools/hwtest.py`.

Format: `### YYYY-MM-DD HH:MM:SS — action` followed by command and result.

---

### 2026-11-13 12:43:52 — Detect board in DFU mode

- Context: the user put the board into the ST system bootloader manually
  (BOOT0 + RESET) and connected USB.
- Command: `STM32_Programmer_CLI -l usb` (read-only)
- Result: OK. 1 device: `USB1`, product `STM32 BOOTLOADER`, serial
  `315A356C3435`, DFU firmware version `0x011a`, device ID `0x0431`
  (STM32F411). Matches the CDC serial seen earlier on this PC (COM10).

### 2026-11-13 12:44:09 — Back up current internal flash (read-only)

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -u 0x08000000 0x80000 stm-boy-flash-backup-20261113.bin`
- Result: OK. 524288 bytes read in 3.6 s. Connect info: DFU 1.1,
  `STM32F411xC/E`, flash 512 KB.
- Saved outside the repo: `C:\Users\Bool\Projects\STM-boy-backups\stm-boy-flash-backup-20261113.bin`,
  SHA-256 `5e44826f5f5050bbd54e888beed822abef7123c0897cbc51c66a55149460ae69`.
- Content: RetroPort firmware (strings "RetroPort", "CHIP-8",
  "ZX Spectrum 48K"); ~354756 bytes used, initial SP `0x20020000`, reset
  vector `0x08001191`.
- Restore command if ever needed:
  `STM32_Programmer_CLI -c port=USB1 -w stm-boy-flash-backup-20261113.bin 0x08000000 -v -s 0x08000000`

### 2026-11-13 12:44:21 — Read option bytes (read-only)

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -ob displ`
- Result: OK. RDP `0xAA` (level 0), PCROP disabled, `BOR_LEV=0x3` (BOR off,
  reset threshold 1.80–2.10 V), `WDG_SW=1` (software watchdog),
  `nRST_STOP=1`, `nRST_STDBY=1`, WRP0–7 not active.
- Note: BOR off is worth reconsidering for battery operation (document
  recommendation only — this project never writes option bytes).
- Board left in DFU mode, waiting for the first hwtest build.

### 2026-11-13 14:56:06 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: none
- DFU (CLI): none
- DFU (PnP): none
