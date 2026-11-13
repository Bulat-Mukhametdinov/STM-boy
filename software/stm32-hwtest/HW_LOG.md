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

### 2026-11-13 15:15:17 — First flash of stm32-hwtest 8cff8b6 (manual note)

- Context: board already in DFU (user, BOOT0+RESET). Overwrites RetroPort (backup taken 12:44:09).
- Command: `py -3.14 tools/hwtest.py flash build/hwtest.bin` (details in the following [auto] entries)

### 2026-11-13 15:15:21 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 3.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.120
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:15:22 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (43336 bytes), expected id: {'fw': '8cff8b6', 'build': 1794572106}
- Programmer: exit code 0, verified=True
- App after start: running fw=8cff8b6 build=1794572106, CDC enumerations=1
- Timing: program=2.98s, dfu_to_app=0.79s, total=3.96s

### 2026-11-13 15:15:31 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=8cff8b6 build=1794572106 clk=96000000 busy=idle reset=por uid=002800343435511331323538

### 2026-11-13 15:15:32 — Run suite selftest (start) [auto]

- Command: `run selftest`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8cff8b6 build=1794572106 busy=idle

### 2026-11-13 15:15:32 — hwtest.py run [auto]

- Command: `hwtest.py run selftest`
- Result: OK (exit code 0)
- Suite selftest: PASS results=4 pass=4 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-151532-selftest.json

### 2026-11-13 15:15:42 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:15:43 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -ob displ`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: RDP : 0xAA (Level 0, no protection)

### 2026-11-13 15:15:46 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v`
- Result: OK (exit code 0, 2.8 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.113
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully

### 2026-11-13 15:15:46 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -u 0x08000000 0xA948 C:\Users\Bool\AppData\Local\Temp\hwtest-fpvzwlgw\upload.bin`
- Result: OK (exit code 0, 0.7 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Data read successfully
- Output: Time elapsed during read operation is: 00:00:00.329

### 2026-11-13 15:15:47 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:15:48 — DFU test cycle 1/20 [auto]

- Command: `trigger=cmd op=write+verify,upload,start`
- Result: PASS
- app->dfu=0.611 s, dfu->app=0.761 s, serial_match=True, upload_match=True, enumerations=1

### 2026-11-13 15:15:48 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:15:49 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:15:49 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:15:50 — DFU test cycle 2/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.617 s, dfu->app=0.736 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:15:51 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:15:51 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:15:54 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:15:55 — DFU test cycle 3/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.609 s, dfu->app=0.76 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:15:55 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.65 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:15:56 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:15:56 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:15:57 — DFU test cycle 4/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.654 s, dfu->app=0.768 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:08 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: FAIL no DFU device within 10 s
- touch: exception (expected): could not open port 'COM10': PermissionError(13, 'Access is denied.', None, 5)

### 2026-11-13 15:16:08 — DFU test cycle 5/20 [auto]

- Command: `trigger=touch+spin op=-`
- Result: FAIL no DFU within 10 s (touch during spin)
- app->dfu=None s, dfu->app=None s, serial_match=None, upload_match=-, enumerations=None

### 2026-11-13 15:16:08 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:16:09 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:09 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:16:10 — DFU test cycle 6/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.594 s, dfu->app=0.775 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:11 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.66 s
- cmd: @OK dfu

### 2026-11-13 15:16:11 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:14 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.116
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:16:15 — DFU test cycle 7/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.658 s, dfu->app=0.757 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:15 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.69 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:16:16 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:16 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:16:17 — DFU test cycle 8/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.689 s, dfu->app=0.773 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:18 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- cmd: @OK dfu

### 2026-11-13 15:16:18 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:21 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.329
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:16:22 — DFU test cycle 9/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.579 s, dfu->app=0.744 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:33 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: FAIL no DFU device within 10 s
- touch: exception (expected): could not open port 'COM10': PermissionError(13, 'Access is denied.', None, 5)

### 2026-11-13 15:16:33 — DFU test cycle 10/20 [auto]

- Command: `trigger=touch+spin op=-`
- Result: FAIL no DFU within 10 s (touch during spin)
- app->dfu=None s, dfu->app=None s, serial_match=None, upload_match=-, enumerations=None

### 2026-11-13 15:16:33 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.64 s
- cmd: @OK dfu

### 2026-11-13 15:16:34 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:37 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.118
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:16:37 — DFU test cycle 11/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.64 s, dfu->app=0.755 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:38 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.67 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:16:38 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:39 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:16:40 — DFU test cycle 12/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.672 s, dfu->app=0.759 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:40 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:16:41 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:44 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.329
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:16:44 — DFU test cycle 13/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.61 s, dfu->app=0.76 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:45 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.70 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:16:45 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:46 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:16:47 — DFU test cycle 14/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.704 s, dfu->app=0.765 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:16:57 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: FAIL no DFU device within 10 s
- touch: exception (expected): could not open port 'COM10': PermissionError(13, 'Access is denied.', None, 5)

### 2026-11-13 15:16:58 — DFU test cycle 15/20 [auto]

- Command: `trigger=touch+spin op=-`
- Result: FAIL no DFU within 10 s (touch during spin)
- app->dfu=None s, dfu->app=None s, serial_match=None, upload_match=-, enumerations=None

### 2026-11-13 15:16:58 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:16:58 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:16:59 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:17:00 — DFU test cycle 16/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.575 s, dfu->app=0.747 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:17:00 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 15:17:01 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:17:04 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.134
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:17:04 — DFU test cycle 17/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.597 s, dfu->app=0.728 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:17:05 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.48 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:17:05 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:17:06 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:17:06 — DFU test cycle 18/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.477 s, dfu->app=0.772 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:17:07 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-13 15:17:07 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:17:10 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.120
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:17:11 — DFU test cycle 19/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.621 s, dfu->app=0.76 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:17:22 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: FAIL no DFU device within 10 s
- touch: exception (expected): could not open port 'COM10': PermissionError(13, 'Access is denied.', None, 5)

### 2026-11-13 15:17:22 — DFU test cycle 20/20 [auto]

- Command: `trigger=touch+spin op=-`
- Result: FAIL no DFU within 10 s (touch during spin)
- app->dfu=None s, dfu->app=None s, serial_match=None, upload_match=-, enumerations=None

### 2026-11-13 15:17:22 — hwtest.py dfu-test [auto]

- Command: `hwtest.py dfu-test --cycles 20 --bin build/hwtest.bin`
- Result: FAIL (exit code 1)
- DFU test: 16/20 cycles passed
- app_to_dfu: min 0.477 / median 0.614 / max 0.704 s
- dfu_to_app: min 0.728 / median 0.76 / max 0.775 s
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-151542-dfu-test.json

### 2026-11-13 15:18:20 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:18:21 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -ob displ`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: RDP : 0xAA (Level 0, no protection)

### 2026-11-13 15:18:24 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v`
- Result: OK (exit code 0, 2.8 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully

### 2026-11-13 15:18:24 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -u 0x08000000 0xA948 C:\Users\Bool\AppData\Local\Temp\hwtest-6e4tgylg\upload.bin`
- Result: OK (exit code 0, 0.7 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Data read successfully
- Output: Time elapsed during read operation is: 00:00:00.328

### 2026-11-13 15:18:25 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:18:25 — DFU test cycle 1/20 [auto]

- Command: `trigger=cmd op=write+verify,upload,start`
- Result: PASS
- app->dfu=0.607 s, dfu->app=0.775 s, serial_match=True, upload_match=True, enumerations=1

### 2026-11-13 15:18:26 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.67 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:18:27 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:27 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:18:28 — DFU test cycle 2/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.674 s, dfu->app=0.772 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:28 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-13 15:18:29 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:32 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.118
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:18:32 — DFU test cycle 3/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.635 s, dfu->app=0.775 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:33 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:18:33 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:34 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:18:35 — DFU test cycle 4/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.58 s, dfu->app=0.762 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:36 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.68 s
- touch: exception on open handle (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:18:36 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:39 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:18:40 — DFU test cycle 5/20 [auto]

- Command: `trigger=touch+spin op=write+verify+start`
- Result: PASS
- app->dfu=0.681 s, dfu->app=0.738 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:41 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.68 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:18:41 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:42 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:18:42 — DFU test cycle 6/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.681 s, dfu->app=0.772 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:43 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-13 15:18:43 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:46 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:18:47 — DFU test cycle 7/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.617 s, dfu->app=0.744 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:48 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:18:48 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:48 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:18:49 — DFU test cycle 8/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.611 s, dfu->app=0.758 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:50 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 15:18:50 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:53 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.113
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:18:54 — DFU test cycle 9/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.598 s, dfu->app=0.757 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:55 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- touch: exception on open handle (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:18:55 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:18:56 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:18:57 — DFU test cycle 10/20 [auto]

- Command: `trigger=touch+spin op=start`
- Result: PASS
- app->dfu=0.6 s, dfu->app=0.761 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:18:57 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.65 s
- cmd: @OK dfu

### 2026-11-13 15:18:58 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:01 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:19:01 — DFU test cycle 11/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.649 s, dfu->app=0.746 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:02 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.65 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:19:02 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:03 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:19:04 — DFU test cycle 12/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.655 s, dfu->app=0.778 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:04 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:19:05 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:08 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.115
- Output: Time elapsed during verifying operation: 00:00:00.330
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:19:08 — DFU test cycle 13/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.612 s, dfu->app=0.764 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:09 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:19:09 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:10 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:19:11 — DFU test cycle 14/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.587 s, dfu->app=0.776 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:12 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.67 s
- touch: exception on open handle (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:19:12 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:15 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.114
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:19:16 — DFU test cycle 15/20 [auto]

- Command: `trigger=touch+spin op=write+verify+start`
- Result: PASS
- app->dfu=0.673 s, dfu->app=0.766 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:17 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.67 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:19:17 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:17 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:19:18 — DFU test cycle 16/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.674 s, dfu->app=0.751 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:19 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 15:19:19 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:22 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.114
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:19:23 — DFU test cycle 17/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.598 s, dfu->app=0.756 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:23 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.51 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:19:24 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:24 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:19:25 — DFU test cycle 18/20 [auto]

- Command: `trigger=touch op=start`
- Result: PASS
- app->dfu=0.51 s, dfu->app=0.762 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:26 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:19:26 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:29 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.120
- Output: Time elapsed during verifying operation: 00:00:00.328
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:19:30 — DFU test cycle 19/20 [auto]

- Command: `trigger=cmd op=write+verify+start`
- Result: PASS
- app->dfu=0.608 s, dfu->app=0.769 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:31 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- touch: exception on open handle (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-13 15:19:31 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435`
- Result: OK (exit code 0, 0.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)

### 2026-11-13 15:19:32 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -s 0x08000000`
- Result: OK (exit code 0, 0.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: Start operation achieved successfully

### 2026-11-13 15:19:32 — DFU test cycle 20/20 [auto]

- Command: `trigger=touch+spin op=start`
- Result: PASS
- app->dfu=0.604 s, dfu->app=0.762 s, serial_match=True, upload_match=-, enumerations=1

### 2026-11-13 15:19:32 — hwtest.py dfu-test [auto]

- Command: `hwtest.py dfu-test --cycles 20 --bin build/hwtest.bin`
- Result: OK (exit code 0)
- DFU test: 20/20 cycles passed
- app_to_dfu: min 0.51 / median 0.611 / max 0.681 s
- dfu_to_app: min 0.738 / median 0.762 / max 0.778 s
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-151820-dfu-test.json

### 2026-11-13 15:18 — DFU test analysis and host fix (manual note)

- First `dfu-test --cycles 20` (15:15:42): 16/20. All 4 failures were the
  "1200-baud touch while `run spin 8000`" cycles (5, 10, 15, 20).
- Cause: host-side bug, not firmware. `hwtest.py` kept the spin connection
  open and tried to open the same COM port a second time for the touch;
  Windows COM ports are exclusive, so SET_LINE_CODING(1200) never reached the
  device. Fix: switch the baud rate to 1200 on the already-open handle.
- Re-run (15:18:20): **20/20 PASS**. app->DFU median 0.61 s (max 0.68),
  DFU->app median 0.76 s (max 0.78), write+verify 2.89 s median, touch during
  a busy main loop works (USB IRQ handles it).
- selftest (15:15:32): PASS 4/4 — flash CRC 35D6F1B7 OK, W25Q JEDEC EF4018,
  UID E463A8F31B121931, 96 MHz from HSE PLL, APB1 48 / APB2 96 MHz, 3 WS.
  HWCAL sector 0xFFF000 reports `foreign` (non-0xFF data present, likely the
  RetroPort ROM pack tail) — nothing was written; investigate before any write.

### 2026-11-13 15:27:12 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=8cff8b6 build=1794572106 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-13 15:28:19 — User visual check of fw 8cff8b6 (manual note)

- Observed by the user on the device: menu and status bar (fw 8cff8b6)
  render correctly; all buttons work in "Button view"; orientation
  (MADCTL 0xA0, landscape) is correct.
- Problem: in "Display info" the 1-px white border is NOT visible on the
  TOP and RIGHT edges. User's estimate: image must move ~1–2 px left and
  ~1 px down (by eye).
- Analysis: with MV=1 the column range is 0..161 (162-wide axis); COLSTART=3
  puts the 160-px window at 3..162, so the last column falls outside the
  panel RAM -> right edge lost; expected xoff in 0..2. Top edge lost ->
  ROWSTART=1 is too small; expected yoff ~2. To be confirmed with the
  interactive display.cal suite (P1).

### 2026-11-13 15:29:31 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.114
- Output: Time elapsed during verifying operation: 00:00:00.327
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:29:32 — hwtest.py dfu-test [auto]

- Command: `hwtest.py dfu-test --wait-lock 60 --manual --step 1 --boot0-wait 240 --bin build/hwtest.bin`
- Result: OK (exit code 0)
- Step boot0: OK
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\unknown\20261113-152928-dfu-test-manual.json

### 2026-11-13 15:29:57 — Manual DFU test (manual note)

- Step 1 (BOOT0+RESET path): PASS at 15:29:28. First attempt at 15:2x failed to start (wrong CLI argument order for --wait-lock, nothing touched the board). DFU detected 0.16 s after start because the user had already pressed BOOT0+RESET; good image flashed and app came back with the expected build.
- Step 2 (recovery rehearsal) starting now: tool flashes a deliberately broken 1 KB image (sector 0 only), checks that CDC does not come back, then the user does BOOT0+RESET and the good image is reflashed.

### 2026-11-13 15:29:59 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-13 15:30:00 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\unknown\broken-1k.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 0.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:00.444
- Output: Time elapsed during verifying operation: 00:00:00.035
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:31:08 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 2.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.114
- Output: Time elapsed during verifying operation: 00:00:00.329
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:31:09 — hwtest.py dfu-test [auto]

- Command: `hwtest.py dfu-test --wait-lock 60 --manual --step 2 --yes --boot0-wait 300 --bin build/hwtest.bin`
- Result: OK (exit code 0)
- Step recovery: OK
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-152958-dfu-test-manual.json

### 2026-11-13 15:31:40 — Manual DFU test step 2 result (manual note)

- Recovery rehearsal: **PASS** (15:29:58). Broken 1 KB image flashed via DFU (sector 0 only), CDC did not reappear for 10 s (board "bricked"), user did BOOT0+RESET 55 s later, DFU detected, good image `8cff8b6` reflashed, app came back with the expected build.
- Conclusion: P0b DFU checks complete — automatic 20/20, BOOT0 path PASS, recovery PASS. Minor tool bug: results of step 1 / broken image landed in `tools/results/unknown/` (info not available at that point).

### 2026-11-13 15:39:40 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-13 15:39:43 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 3.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.224
- Output: Time elapsed during verifying operation: 00:00:00.357
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:39:44 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin --wait-lock 900`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (48320 bytes), expected id: {'fw': '7734c40+dirty', 'build': 1794573569}
- Programmer: exit code 0, verified=True
- App after start: running fw=7734c40+dirty build=1794573569, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=3.02s, dfu_to_app=0.77s, total=4.50s

### 2026-11-13 15:39:45 — Run suite display.info (start) [auto]

- Command: `run display.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794573569 busy=idle

### 2026-11-13 15:39:45 — hwtest.py run [auto]

- Command: `hwtest.py run display.info`
- Result: OK (exit code 0)
- Suite display.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-153945-display.info.json

### 2026-11-13 15:39:51 — Run suite display.cal (start) [auto]

- Command: `run display.cal t=4`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794573569 busy=idle

### 2026-11-13 15:39:56 — hwtest.py run [auto]

- Command: `hwtest.py run display.cal t=4`
- Result: FAIL (exit code 2)
- Suite display.cal: ABORT results=2 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-153951-display.cal.json

### 2026-11-13 15:39:56 — hwtest.py info [auto]

- Command: `hwtest.py info --json`
- Result: OK (exit code 0)
- COM10: fw=7734c40+dirty build=1794573569 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-13 15:40:00 — P1 early build: display.cal + provisional LCD offsets (manual note)

- Flashed an early P1 build (7734c40+dirty) so the user can run `display.cal` now.
- Compiled LCD defaults changed from xoff=3/yoff=1 (stm32-bringup) to **xoff=2/yoff=2**
  (provisional): user reported on P0 that the 1 px border was missing on the top and right
  edges. With MADCTL 0xA0 (MV=1) the column axis is 162 px, so xoff=3 puts x=159 at RAM
  column 162 (out of range) -> valid xoff 0..2; yoff range 0..4. `display.cal` is the
  source of truth; the result is kept in RAM only (no W25Q writes in P1).
- Backlight now runs from TIM3_CH1 PWM on PA6 (AF2), full on by default.
- Result: flash OK; `display.info` PASS (SPI1 BIDI TX-only, PA5/PA7 AF5, PA6 AF2 = TIM3,
  full-frame fill 27.4 ms at 12 MHz); `display.cal t=4` -> prompt shown, timed out cleanly
  (@A timeout, @DONE abort, board idle).

### 2026-11-13 15:40:34 — Run suite display.cal (start) [auto]

- Command: `run display.cal`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794573569 busy=idle

### 2026-11-13 15:40:35 — hwtest.py run [auto]

- Command: `hwtest.py run display.cal --wait-lock 900`
- Result: FAIL (exit code 1)
- Suite display.cal: FAIL results=2 pass=1 fail=1
- Failed: display.cal/offset
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-154034-display.cal.json

### 2026-11-13 15:41:03 — display.cal (landscape) attempt 1 (manual note)

- Result: cancelled — user pressed B by accident before nudging (start xoff=2 yoff=2). Restarting.

### 2026-11-13 15:41:04 — Run suite display.cal (start) [auto]

- Command: `run display.cal`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794573569 busy=idle

### 2026-11-13 15:41:40 — hwtest.py run [auto]

- Command: `hwtest.py run display.cal --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.cal: PASS results=11 pass=11 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-154104-display.cal.json

### 2026-11-13 15:42:04 — display.cal (landscape) attempt 2 (manual note)

- Command: `py -3.14 tools/hwtest.py run display.cal` (fw 7734c40+dirty)
- User nudged xoff 2->1->0->1->2->1->0->1 and yoff 2->3->2, saved with A.
- Result: **PASS. Landscape MADCTL 0xA0: xoff=1, yoff=2** (valid ranges xoff 0..2, yoff 0..4). All 4 border edges visible, no magenta.
- Old bring-up values COLSTART=3/ROWSTART=1 were wrong (right column and top row lost).

### 2026-11-13 15:42:41 — Run suite display.cal (start) [auto]

- Command: `run display.cal port`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794573569 busy=idle

### 2026-11-13 15:43:10 — hwtest.py run [auto]

- Command: `hwtest.py run display.cal port --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.cal: PASS results=2 pass=2 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-154241-display.cal.json

### 2026-11-13 15:43:22 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 15:43:26 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 3.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.625
- Output: Time elapsed during verifying operation: 00:00:00.372
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:43:26 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin --wait-lock 900`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (50952 bytes), expected id: {'fw': '7734c40+dirty', 'build': 1794573792}
- Programmer: exit code 0, verified=True
- App after start: running fw=7734c40+dirty build=1794573792, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=3.45s, dfu_to_app=0.77s, total=4.90s

### 2026-11-13 15:43:27 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 0 256 --save-bin tools/results/tmp`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:43:39 — display.cal (portrait) (manual note)

- Command: `py -3.14 tools/hwtest.py run display.cal port` (fw 7734c40+dirty)
- Result: **PASS, saved without nudging: portrait MADCTL 0x00 xoff=2, yoff=1** (ranges x 0..4, y 0..2). Orientation of MADCTL 0x00 not yet validated (orientation suite pending).
- Cross-check: consistent with landscape (0xA0) xoff=1/yoff=2 — same physical RAM offsets with axes swapped (132-axis offset 2, 162-axis offset 1).

### 2026-11-13 15:43:53 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q scan 0 0x1000000 0x10000 --timeout 120`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "000000", "len": 16777216, "step": 65536, "granules": 256, "blank": 250, "used": 6, "nonff_bytes": 207297, "first_nonff": "000000", "last_nonff": "FFFFFF", "ranges": [["0000

### 2026-11-13 15:43:55 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q scan F00000 0x100000 4096 --timeout 60`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "F00000", "len": 1048576, "step": 4096, "granules": 256, "blank": 255, "used": 1, "nonff_bytes": 4096, "first_nonff": "FFF000", "last_nonff": "FFFFFF", "ranges": [["FFF000",

### 2026-11-13 15:44:30 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 000000 256 --save-bin tools/results/002800343435511331323538/w25q/a000000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:44:31 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 100000 256 --save-bin tools/results/002800343435511331323538/w25q/a100000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:44:32 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read FFF000 4096 --save-bin tools/results/002800343435511331323538/w25q/aFFF000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:45:15 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 100000 65536 --timeout 30 --save-bin tools/results/002800343435511331323538/w25q/pack_100000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:45:16 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 110000 65536 --timeout 30 --save-bin tools/results/002800343435511331323538/w25q/pack_110000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:45:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 120000 65536 --timeout 30 --save-bin tools/results/002800343435511331323538/w25q/pack_120000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:45:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q read 130000 0x3670 --timeout 30 --save-bin tools/results/002800343435511331323538/w25q/pack_130000`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-13 15:45:27 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q scan 0 0x100000 4096 --timeout 30`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "000000", "len": 1048576, "step": 4096, "granules": 256, "blank": 255, "used": 1, "nonff_bytes": 644, "first_nonff": "000000", "last_nonff": "0002DF", "ranges": [["000000",

### 2026-11-13 15:45:29 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q scan 100000 0x100000 4096 --timeout 30`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "100000", "len": 1048576, "step": 4096, "granules": 256, "blank": 204, "used": 52, "nonff_bytes": 202557, "first_nonff": "100000", "last_nonff": "13366A", "ranges": [["10000

### 2026-11-13 15:45:59 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd w25q crc 0 0x1000000 --timeout 60`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q crc lines=2
- @R {"t": "w25q_crc", "addr": "000000", "len": 16777216, "crc32": "8C5A3340", "ms": 28654, "spi_hz": 12000000}

### 2026-11-13 15:47:00 — W25Q128 read-only layout investigation (manual note)

Why: HWCAL sector 0xFFF000 reported `foreign`. New read-only commands `w25q scan/read/crc`
(nothing written). Dumps: `tools/results/002800343435511331323538/w25q/` (.bin are
git-ignored; .txt hexdumps next to them).

- Whole chip, 64 KB granules: 250/256 blank; used = `000000-010000`, `100000-140000`,
  `FF0000-1000000`. Whole-chip CRC-32 (zlib) = `8C5A3340` (16 MB in 28.7 s at 12 MHz).
- `000000-001000` (4 KB scan): stm32-retroport settings log, 16-byte records with magic
  `RFS1` (0x31534652), 644 non-FF bytes, rest of the first 1 MB blank.
- `100000-134000`: RetroPort ROM pack, `RPRP` v2, header 64 B, entry 72 B, **142 entries**
  (110 CHIP-8, 31 SCHIP, 1 ZX48), data_offset 0x2830, data 0x30E40 B, pack_size
  **0x33670 (205.6 KB)** -> ends at 0x133670 (last ROM byte 0x13366A). Pack CRC and all 142
  per-ROM CRCs verify; bytes after pack_size in the last sector are 0xFF.
- Last 1 MB `F00000-FFFFFF` (4 KB scan): 255/256 sectors blank; **only 0xFFF000-0xFFFFFF is
  used**, and it holds a pure walking-ones test pattern `01 02 04 08 10 20 40 80` repeated
  over all 4096 bytes. It is not RPRP/RFS1/HWCAL data and nothing in this repo (bringup,
  retroport, hwtest, git history) writes it -> leftover of some earlier flash test (factory
  or an uncommitted test firmware).
- Result: **the reserved hwtest region does not overlap any user data.** The ROM pack uses
  ~1.4 % of its 14 MiB window and is ~13.8 MB away from 0xF00000. HWCAL `foreign` is caused
  only by the test pattern; `hwcal init force` would be safe (not done: W25Q writes are
  forbidden in P1; the user decides).

### 2026-11-13 15:49:02 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-13 15:49:06 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 3.6 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.749
- Output: Time elapsed during verifying operation: 00:00:00.414
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:49:07 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin --wait-lock 900`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (56160 bytes), expected id: {'fw': '7734c40+dirty', 'build': 1794574126}
- Programmer: exit code 0, verified=True
- App after start: running fw=7734c40+dirty build=1794574126, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.63s, program=3.63s, dfu_to_app=0.76s, total=5.10s

### 2026-11-13 15:49:07 — Run suite pins (start) [auto]

- Command: `run pins`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794574126 busy=idle

### 2026-11-13 15:49:09 — hwtest.py run [auto]

- Command: `hwtest.py run pins`
- Result: FAIL (exit code 1)
- Suite pins: FAIL results=44 pass=43 fail=1
- Failed: pins/PB2
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-154907-pins.json

### 2026-11-13 15:49:51 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 15:49:55 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 3.6 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:02.757
- Output: Time elapsed during verifying operation: 00:00:00.414
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 15:49:56 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin --wait-lock 900`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (56528 bytes), expected id: {'fw': '7734c40+dirty', 'build': 1794574188}
- Programmer: exit code 0, verified=True
- App after start: running fw=7734c40+dirty build=1794574188, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=3.62s, dfu_to_app=0.76s, total=5.06s

### 2026-11-13 15:49:57 — Run suite pins (start) [auto]

- Command: `run pins`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7734c40+dirty build=1794574188 busy=idle

### 2026-11-13 15:49:59 — hwtest.py run [auto]

- Command: `hwtest.py run pins`
- Result: FAIL (exit code 1)
- Suite pins: FAIL results=46 pass=43 fail=1
- Failed: pins/PB2
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-154957-pins.json

### 2026-11-13 15:50:00 — pins suite results (manual note)

- Firmware 7734c40+dirty build 1794574188 (P1 work in progress). `run pins`: 29/30 pins as
  expected, 13/13 bridge tests clean (no neighbour follows an open-drain low), W25Q JEDEC OK
  after the restore, whole run 1.18 s.
- Expected and seen: PA0 CHRG TIED_HIGH (not charging), PA2 OFF_REQ WEAK_HIGH (100k),
  PA3 OFF_ACK FLOATING, PA4/PA15 TIED_HIGH (10k), PA6 BLK TIED_LOW (NPN base), PA9 VBUS
  TIED_HIGH, PA5/PA7/PB0/PB1/PB3/PB4/PB5/PA8 FLOATING, all 9 buttons FLOATING, PB10/PC13-15
  FLOATING. PA1 BAT_STAT reads 1 (info only).
- **Mismatch: PB2 AMP_SD reads TIED_LOW, expected WEAK_LOW (R23 100k).** With the internal
  pull-up (~40k) the pin did not read 1 within 500 us (re-run with rise-time measurement:
  rise_us = 500 = never). 100k alone would give ~2.4 V. So the effective pull-down is well
  below ~40k: either the PAM8302A SD pin has its own pull-down, or R23 is not 100k / there
  is a leak to GND. Harmless for function (amp stays off), but worth checking with a
  multimeter (resistance PB2 net to GND with the board off) before relying on a weak
  pull-down there.

### 2026-11-13 16:00:00 — hardware work stopped (manual note)

- The user unplugged the board. Last firmware on the board: 7734c40+dirty build 1794574188
  (has display.info/display.cal/pins/W25Q commands; NOT the final P1 build).
- Final P1 build (display.orient/rgb/invert/gamma/bars/gradient/spi, backlight,
  backlight.user, input.live/chord/bounce, Tests menu) is built only: zero warnings at
  -O2/-Os/-O3, never flashed or run. Next session: `make dfu`, then `run selftest`, `run pins`,
  `run backlight`, `run display.info`, then the [U] suites (see README / PROTOCOL.md).

- Correction (16:08:32): the "16:00:00" stop-note time above is approximate; final -O2 build
  is 7734c40+dirty build 1794575292 (zero warnings at -O2/-Os/-O3), not flashed.

### 2026-11-13 20:17:19 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-13 20:17:38 — Session resumed (manual note)

- Board reconnected on COM10 (running build 1794574188). Flashing final P1 build (8da7e38) and running automatic suites.

### 2026-11-13 20:17:39 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 20:17:45 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 5.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:04.266
- Output: Time elapsed during verifying operation: 00:00:00.553
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 20:17:46 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (77252 bytes), expected id: {'fw': '8da7e38+dirty', 'build': 1794590256}
- Programmer: exit code 0, verified=True
- App after start: running fw=8da7e38+dirty build=1794590256, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=5.26s, dfu_to_app=0.78s, total=6.71s

### 2026-11-13 20:17:52 — Run suite selftest (start) [auto]

- Command: `run selftest`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:17:52 — hwtest.py run [auto]

- Command: `hwtest.py run selftest`
- Result: OK (exit code 0)
- Suite selftest: PASS results=4 pass=4 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201751-selftest.json

### 2026-11-13 20:17:52 — Run suite pins (start) [auto]

- Command: `run pins`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:17:54 — hwtest.py run [auto]

- Command: `hwtest.py run pins`
- Result: FAIL (exit code 1)
- Suite pins: FAIL results=46 pass=43 fail=1
- Failed: pins/PB2
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201752-pins.json

### 2026-11-13 20:17:55 — Run suite backlight (start) [auto]

- Command: `run backlight`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:17:56 — hwtest.py run [auto]

- Command: `hwtest.py run backlight`
- Result: FAIL (exit code 1)
- Suite backlight: FAIL results=9 pass=5 fail=4
- Failed: backlight/pwm_200hz_50pct, backlight/pwm_1000hz_50pct, backlight/pwm_5000hz_50pct, backlight/pwm_25000hz_50pct
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201755-backlight.json

### 2026-11-13 20:17:56 — Run suite display.info (start) [auto]

- Command: `run display.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:17:56 — hwtest.py run [auto]

- Command: `hwtest.py run display.info`
- Result: OK (exit code 0)
- Suite display.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201756-display.info.json

### 2026-11-13 20:18:38 — Automatic suites on fw 8da7e38+dirty (manual note)

- selftest PASS; display.info PASS; pins 43/44 PASS — only PB2 AMP_SD again TIED_LOW (expected WEAK_LOW, R23 100k) -> check net to GND with multimeter / PAM8302A SD input.
- backlight: registers OK, frequency exact (200/1000/5000/25000 Hz measured = requested) but measured duty 47.1–47.6 % for 50 % requested at ALL frequencies -> constant ratio, not an edge-delay effect; suspected IDR-sampling artefact. To verify with scope on PA6 / Q1 base.
- Starting batch of [U] suites (display.orient .. input.bounce), t=300 s each.

### 2026-11-13 20:18:39 — Run suite display.orient (start) [auto]

- Command: `run display.orient land t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:19:23 — hwtest.py run [auto]

- Command: `hwtest.py run display.orient land t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.orient: PASS results=8 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201839-display.orient.json

### 2026-11-13 20:19:23 — Run suite display.orient (start) [auto]

- Command: `run display.orient port t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:19:40 — hwtest.py run [auto]

- Command: `hwtest.py run display.orient port t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.orient: PASS results=6 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201923-display.orient.json

### 2026-11-13 20:19:41 — Run suite display.rgb (start) [auto]

- Command: `run display.rgb t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:19:58 — hwtest.py run [auto]

- Command: `hwtest.py run display.rgb t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.rgb: PASS results=4 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201941-display.rgb.json

### 2026-11-13 20:19:59 — Run suite display.invert (start) [auto]

- Command: `run display.invert t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:20:42 — hwtest.py run [auto]

- Command: `hwtest.py run display.invert t=300 --wait-lock 900`
- Result: FAIL (exit code 1)
- Suite display.invert: FAIL results=3 pass=0 fail=1
- Failed: display.invert/invert
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-201959-display.invert.json

### 2026-11-13 20:20:43 — Run suite display.gamma (start) [auto]

- Command: `run display.gamma t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:21:25 — hwtest.py run [auto]

- Command: `hwtest.py run display.gamma t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.gamma: PASS results=25 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202043-display.gamma.json

### 2026-11-13 20:21:25 — Run suite display.bars (start) [auto]

- Command: `run display.bars t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:22:14 — hwtest.py run [auto]

- Command: `hwtest.py run display.bars t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.bars: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202125-display.bars.json

### 2026-11-13 20:22:14 — Run suite display.gradient (start) [auto]

- Command: `run display.gradient t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:22:42 — hwtest.py run [auto]

- Command: `hwtest.py run display.gradient t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.gradient: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202214-display.gradient.json

### 2026-11-13 20:22:43 — Run suite display.spi (start) [auto]

- Command: `run display.spi div=8 t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:24:03 — hwtest.py run [auto]

- Command: `hwtest.py run display.spi div=8 t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.spi: PASS results=5 pass=4 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202243-display.spi.json

### 2026-11-13 20:24:04 — Run suite display.spi (start) [auto]

- Command: `run display.spi div=4 t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:25:22 — hwtest.py run [auto]

- Command: `hwtest.py run display.spi div=4 t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.spi: PASS results=5 pass=4 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202404-display.spi.json

### 2026-11-13 20:25:23 — Run suite display.spi (start) [auto]

- Command: `run display.spi div=2 t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:26:02 — hwtest.py run [auto]

- Command: `hwtest.py run display.spi div=2 t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite display.spi: PASS results=5 pass=4 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202523-display.spi.json

### 2026-11-13 20:26:02 — Run suite backlight.user (start) [auto]

- Command: `run backlight.user t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:27:57 — hwtest.py run [auto]

- Command: `hwtest.py run backlight.user t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite backlight.user: PASS results=5 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202602-backlight.user.json

### 2026-11-13 20:27:57 — Run suite input.live (start) [auto]

- Command: `run input.live t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:28:19 — hwtest.py run [auto]

- Command: `hwtest.py run input.live t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite input.live: PASS results=29 pass=9 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202757-input.live.json

### 2026-11-13 20:28:20 — Run suite input.chord (start) [auto]

- Command: `run input.chord t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:33:34 — hwtest.py run [auto]

- Command: `hwtest.py run input.chord t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite input.chord: PASS results=14 pass=11 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-202820-input.chord.json

### 2026-11-13 20:33:34 — Run suite input.bounce (start) [auto]

- Command: `run input.bounce t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=8da7e38+dirty build=1794590256 busy=idle

### 2026-11-13 20:35:31 — hwtest.py run [auto]

- Command: `hwtest.py run input.bounce t=300 --wait-lock 900`
- Result: OK (exit code 0)
- Suite input.bounce: PASS results=11 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-203334-input.bounce.json

### 2026-11-13 20:36:10 — [U] suite batch results (manual note)

- display.orient: land 0xA0 x1/y2 and port 0x00 x2/y1 confirmed. display.rgb: RGB (bgr=0). display.gamma: user chose `sitronix` (id 2). display.bars / display.gradient: PASS.
- display.invert: FAIL with invert=null (no option selected) — to clarify with user / rerun.
- display.spi: stable (no flicker) at 12, 24 and **48 MHz** (div 8/4/2, 8- and 16-bit frames, both GPIO speeds) at SYSCLK 96 MHz. Datasheet limit ~15 MHz; 48 MHz is the max SPI1 clock at 96 MHz.
- backlight.user: chosen PWM 1 kHz, min usable level 0 (lowest step).
- input.live / input.chord: PASS, all 9 buttons, chords OK. POWER excluded (ATtiny behaviour).
- input.bounce (30 presses each): most buttons <0.5 ms; **B release bounce up to 17.6 ms (p95 5.6 ms, rec 12 ms)**; Y up to 1.5 ms; DOWN max 4.0 ms. Pooled rule gave 5 ms, which is NOT safe for B -> recommend 20 ms (or press-immediate / release 20 ms).
- Backlight duty question (47.4 % measured vs 50 %): pending scope check by user.
