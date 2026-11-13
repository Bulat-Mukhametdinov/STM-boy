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
