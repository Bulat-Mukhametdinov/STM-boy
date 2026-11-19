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

### 2026-11-13 20:49:27 — User decisions (manual note)

- display.invert: user confirms picture was correct with inversion OFF (forgot to press A) -> invert=0.
- User permits using W25Q sectors freely (incl. overwriting the 0xFFF000 test pattern for HWCAL). Policy kept: RetroPort settings (0x000000) and ROM pack (0x100000..) are still not touched.
- Backlight duty scope check postponed (scope available in ~2 days).

### 2026-11-13 20:56:28 — SWD probe check (manual note)

- User connected ST-Link V2 (GND/SWDIO/SWCLK/NRST, **no 3V3 wire**). Probe SN 37FF71064E573436CD711443, FW V2J47S7.
- Command: `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0xE0042000 1 -r32 0x2001FF00 8` (read-only, no reset, app kept running on USB)
- Result: OK. Voltage reported 3.25 V (with no VAPP wire this is most likely a clone reading its own supply). DBGMCU IDCODE 0x10006431 -> DEV_ID 0x431, **REV_ID 0x1000 = Rev A**; BL version 0xD0. `.noinit` at 0x2001FF00 readable live (valid magic A5A5C3C3) -> fault breadcrumbs can be read over SWD without reset.
- Note: SWD must be disconnected (or at least verified not to back-power) before power/handshake/current tests.

### 2026-11-13 20:57:56 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 20:58:05 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 9.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:07.912
- Output: Time elapsed during verifying operation: 00:00:01.071
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 20:58:06 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (152928 bytes), expected id: {'fw': '7cd4231+dirty', 'build': 1794592561}
- Programmer: exit code 0, verified=True
- App after start: running fw=7cd4231+dirty build=1794592561, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=9.42s, dfu_to_app=0.78s, total=10.91s

### 2026-11-13 20:58:45 — Run suite w25q.info (start) [auto]

- Command: `run w25q.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 20:58:45 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.info --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite w25q.info: PASS results=3 pass=3 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-205845-w25q.info.json

### 2026-11-13 20:58:46 — Run suite w25q.readcrc (start) [auto]

- Command: `run w25q.readcrc`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 20:59:50 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.readcrc --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite w25q.readcrc: PASS results=4 pass=4 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-205846-w25q.readcrc.json

### 2026-11-13 20:59:50 — Run suite flash.check (start) [auto]

- Command: `run flash.check`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 20:59:51 — hwtest.py run [auto]

- Command: `hwtest.py run flash.check --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite flash.check: PASS results=3 pass=3 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-205950-flash.check.json

### 2026-11-13 20:59:51 — Run suite ram.march (start) [auto]

- Command: `run ram.march`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 20:59:51 — hwtest.py run [auto]

- Command: `hwtest.py run ram.march --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite ram.march: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-205951-ram.march.json

### 2026-11-13 20:59:52 — Run suite w25q.pd (start) [auto]

- Command: `run w25q.pd`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 20:59:52 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.pd --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite w25q.pd: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-205952-w25q.pd.json

### 2026-11-13 21:00:03 — Run suite w25q.sector (start) [auto]

- Command: `run w25q.sector`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 21:00:04 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.sector --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite w25q.sector: PASS results=3 pass=3 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-210003-w25q.sector.json

### 2026-11-13 21:00:05 — Run suite w25q.scratch (start) [auto]

- Command: `run w25q.scratch confirm=yes`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 21:00:12 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.scratch confirm=yes --wait-lock 900 --timeout 120`
- Result: OK (exit code 0)
- Suite w25q.scratch: PASS results=10 pass=10 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-210005-w25q.scratch.json

### 2026-11-13 21:00:29 — Run suite bench.all (start) [auto]

- Command: `run bench.all`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592561 busy=idle

### 2026-11-13 21:00:46 — hwtest.py run [auto]

- Command: `hwtest.py run bench.all --wait-lock 900 --timeout 900`
- Result: OK (exit code 0)
- Suite bench.all: PASS results=281 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-210028-bench.all.json

### 2026-11-13 21:03:20 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-13 21:03:30 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 9.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:07.911
- Output: Time elapsed during verifying operation: 00:00:01.071
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:03:30 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (152992 bytes), expected id: {'fw': '7cd4231+dirty', 'build': 1794592997}
- Programmer: exit code 0, verified=True
- App after start: running fw=7cd4231+dirty build=1794592997, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.63s, program=9.41s, dfu_to_app=0.78s, total=10.91s

### 2026-11-13 21:03:32 — hwtest.py usbbench [auto]

- Command: `hwtest.py usbbench --wait-lock 900`
- Result: OK (exit code 0)
- usbbench tx: {"status": "ok", "received": 262144, "crc_ok": true, "host_s": 0.4158, "host_kBps": 630.4, "device_kBps": 1008}
- usbbench rx: {"status": "ok", "device_got": 262144, "host_s": 0.277, "host_kBps": 946.2, "device_kBps": 946}

### 2026-11-13 21:03:32 — Run suite bench.all (start) [auto]

- Command: `run bench.all`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794592997 busy=idle

### 2026-11-13 21:03:50 — hwtest.py run [auto]

- Command: `hwtest.py run bench.all --wait-lock 900 --timeout 900`
- Result: OK (exit code 0)
- Suite bench.all: PASS results=281 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-210332-bench.all.json

### 2026-11-13 21:04:07 — hwtest.py usbbench [auto]

- Command: `hwtest.py usbbench --wait-lock 900`
- Result: OK (exit code 0)
- usbbench tx: {"status": "ok", "received": 262144, "crc_ok": true, "host_s": 0.418, "host_kBps": 627.1, "device_kBps": 999}
- usbbench rx: {"status": "ok", "device_got": 262144, "host_s": 0.2698, "host_kBps": 971.7, "device_kBps": 972}

### 2026-11-13 21:06:45 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 21:06:55 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 9.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:07.974
- Output: Time elapsed during verifying operation: 00:00:01.071
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:06:56 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (152960 bytes), expected id: {'fw': '7cd4231+dirty', 'build': 1794593179}
- Programmer: exit code 0, verified=True
- App after start: running fw=7cd4231+dirty build=1794593179, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=9.48s, dfu_to_app=0.78s, total=10.94s

### 2026-11-13 21:06:57 — hwtest.py usbbench [auto]

- Command: `hwtest.py usbbench --wait-lock 900`
- Result: OK (exit code 0)
- usbbench tx: {"status": "ok", "received": 262144, "crc_ok": true, "host_s": 0.4167, "host_kBps": 629.0, "device_kBps": 1004}
- usbbench rx: {"status": "ok", "device_got": 262144, "host_s": 0.2734, "host_kBps": 958.9, "device_kBps": 959}

### 2026-11-13 21:06:58 — Run suite bench.mem (start) [auto]

- Command: `run bench.mem`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794593179 busy=idle

### 2026-11-13 21:06:58 — hwtest.py run [auto]

- Command: `hwtest.py run bench.mem --wait-lock 900`
- Result: OK (exit code 0)
- Suite bench.mem: PASS results=53 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-210658-bench.mem.json

### 2026-11-13 21:06:59 — Run suite bench.irq (start) [auto]

- Command: `run bench.irq`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794593179 busy=idle

### 2026-11-13 21:06:59 — hwtest.py run [auto]

- Command: `hwtest.py run bench.irq --wait-lock 900`
- Result: OK (exit code 0)
- Suite bench.irq: PASS results=5 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-210659-bench.irq.json

### 2026-11-13 21:10 — P3/P5 results (manual note)

- P3 suites w25q.info/readcrc/pd/sector/scratch(confirm=yes), ram.march, flash.check: all PASS on hardware. Scratch area was blank beforehand.
- bench.all PASS (-O2). Insights: DMA W25Q read 2.99 MB/s @24 MHz (polling 1.13); SPI1 48 MHz 16-bit frame 6.83 ms (146 fps), 8-bit polling only 72 % bus utilisation; running code from RAM is slower than flash with ART (S-bus fetch); DMA2 mem2mem never beats LDM/STM (CPU) copy, only newlib-nano (byte loop).
- HWCAL write of the P1 calibration was NOT done: the auto-mode permission check denied the `hwcal set ... / hwcal save force` command (instruction reached me only via a relayed message). Firmware support is in place (`hwcal set key=value ...`, `hwcal save [force]`, boot applies display/backlight/debounce); needs an explicit user go-ahead.

### 2026-11-13 21:08:21 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set madctl_land=0xA0 xoff_land=1 yoff_land=2 madctl_port=0 xoff_port=2 yoff_port=1 bgr=0 invert=0 gamma_id=2 spi1_div_ok=2 spi1_div_max=2 bl_pwm_hz=1000 bl_min_level=0 debounce_ms=20"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR line too long (max 120 chars) lines=1

### 2026-11-13 21:08:31 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set madctl_land=0xA0 xoff_land=1 yoff_land=2 madctl_port=0 xoff_port=2 yoff_port=1 bgr=0 invert=0"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-13 21:08:31 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set gamma_id=2 spi1_div_ok=2 spi1_div_max=2 bl_pwm_hz=1000 bl_min_level=0 debounce_ms=20"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-13 21:09:00 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save force"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR hwcal flash write/verify failed lines=1

### 2026-11-13 21:09:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FFF000 1000 100"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q step must be a power of two 256..1M; addr/len multiples of step lines=1

### 2026-11-13 21:09:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "foreign", "seq": 0, "slot": "-", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "g

### 2026-11-13 21:09:25 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FFF000 0x1000 0x100"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "FFF000", "len": 4096, "step": 256, "granules": 16, "blank": 0, "used": 16, "nonff_bytes": 4096, "first_nonff": "FFF000", "last_nonff": "FFFFFF", "ranges": [["FFF000", "1000

### 2026-11-13 21:10:03 — Run suite w25q.info (start) [auto]

- Command: `run w25q.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794593179 busy=idle

### 2026-11-13 21:10:03 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.info`
- Result: OK (exit code 0)
- Suite w25q.info: PASS results=3 pass=3 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-211003-w25q.info.json

### 2026-11-13 21:10:13 — Run suite w25q.sector (start) [auto]

- Command: `run w25q.sector addr=0xFFF000`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=7cd4231+dirty build=1794593179 busy=idle

### 2026-11-13 21:10:13 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.sector addr=0xFFF000`
- Result: OK (exit code 0)
- Suite w25q.sector: PASS results=3 pass=3 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-211013-w25q.sector.json

### 2026-11-13 21:12:11 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 21:12:21 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 9.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:07.962
- Output: Time elapsed during verifying operation: 00:00:01.095
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:12:22 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (154096 bytes), expected id: {'fw': '7cd4231+dirty', 'build': 1794593502}
- Programmer: exit code 0, verified=True
- App after start: running fw=7cd4231+dirty build=1794593502, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=9.50s, dfu_to_app=0.78s, total=10.96s

### 2026-11-13 21:12:26 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q erase4k FDF000"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q erase4k lines=2
- @R {"t": "w25q_erase4k", "addr": "FDF000", "st": 0, "st_s": "ok", "sr1_before": "00", "sr1": "00", "us": 61414, "first16_blank": true}

### 2026-11-13 21:12:56 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FF0000 10000 1000"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q step must be a power of two 256..1M; addr/len multiples of step lines=1

### 2026-11-13 21:13:03 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FF0000 10000 1000"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q step must be a power of two 256..1M; addr/len multiples of step lines=1

### 2026-11-13 21:13:04 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FF0000 10000 1000"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q step must be a power of two 256..1M; addr/len multiples of step lines=1

### 2026-11-13 21:13:12 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FF0000 10000 4096"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q step must be a power of two 256..1M; addr/len multiples of step lines=1

### 2026-11-13 21:13:27 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FF0000 65536 4096"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "FF0000", "len": 65536, "step": 4096, "granules": 16, "blank": 15, "used": 1, "nonff_bytes": 4096, "first_nonff": "FFF000", "last_nonff": "FFFFFF", "ranges": [["FFF000", "10

### 2026-11-13 21:14:03 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 21:14:12 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 9.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:07.950
- Output: Time elapsed during verifying operation: 00:00:01.084
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:14:13 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (154096 bytes), expected id: {'fw': '7cd4231+dirty', 'build': 1794593640}
- Programmer: exit code 0, verified=True
- App after start: running fw=7cd4231+dirty build=1794593640, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=9.48s, dfu_to_app=0.78s, total=10.94s

### 2026-11-13 21:14:14 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q lock FFF000"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q usage: w25q id | read <addr_hex> <len> | crc <addr_hex> <len> | scan <addr_hex> <len> [step] lines=1

### 2026-11-13 21:14:16 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q lock FDF000"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q usage: w25q id | read <addr_hex> <len> | crc <addr_hex> <len> | scan <addr_hex> <len> [step] lines=1

### 2026-11-13 21:14:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q erase4k FFE000"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q erase4k lines=2
- @R {"t": "w25q_erase4k", "addr": "FFE000", "st": 0, "st_s": "ok", "sr1_before": "00", "sr1": "00", "us": 56726, "first16_blank": true}

### 2026-11-13 21:14:23 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q lock FFF000"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q usage: w25q id | read <addr_hex> <len> | crc <addr_hex> <len> | scan <addr_hex> <len> [step] lines=1

### 2026-11-13 21:14:40 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- cmd: @OK dfu

### 2026-11-13 21:14:50 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 9.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:07.965
- Output: Time elapsed during verifying operation: 00:00:01.085
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:14:50 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (154640 bytes), expected id: {'fw': '7cd4231+dirty', 'build': 1794593672}
- Programmer: exit code 0, verified=True
- App after start: running fw=7cd4231+dirty build=1794593672, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.58s, program=9.50s, dfu_to_app=0.78s, total=10.94s

### 2026-11-13 21:14:57 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q lock FFF000"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q lock lines=2
- @R {"t": "w25q_lock", "addr": "FFF000", "lock": "01", "sr1": "00", "sr2": "02", "sr3": "60"}

### 2026-11-13 21:14:58 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q lock FDF000"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q lock lines=2
- @R {"t": "w25q_lock", "addr": "FDF000", "lock": "01", "sr1": "00", "sr2": "02", "sr3": "60"}

### Manual note 2026-11-13: hwcal save force -> "flash write/verify failed", sector 0xFFF000 unchanged
- Code review found no logic bug in w25q_erase_sector/hwcal_save (range check at top of array OK: write_ok(0xFFF000,4096) true; wait_ready timeouts 10/1000 ms adequate; WREN/CS handling identical to w25q_erase_cmd which works).
- Reproduction on non-HWCAL addresses: `w25q erase4k FDF000` and `FFE000` (4K sector right below HWCAL) both OK, ~57-61 ms, SR1=00. SR1=00 SR2=02 SR3=60 (WPS=0, so 0x3D lock bits, which read 01 everywhere, are ignored). Root cause NOT yet proven; HWCAL sector itself not touched by this session.
- Added: hwcal errors now report `step=erase|erase_verify|program_hdr|program_body|program_magic|verify_read|verify_compare st= sr1= sr2= sr3=`; `w25q erase4k <addr_hex> [force]` (0xFFF000 needs force); `w25q lock <addr_hex>` (read-only 0x3D).

### 2026-11-13 21:15:39 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q erase4k FFF000 force"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q erase4k lines=2
- @R {"t": "w25q_erase4k", "addr": "FFF000", "st": 0, "st_s": "ok", "sr1_before": "00", "sr1": "00", "us": 86221, "first16_blank": true}

### 2026-11-13 21:15:48 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set madctl_land=0xA0 xoff_land=1 yoff_land=2 madctl_port=0 xoff_port=2 yoff_port=1 bgr=0 invert=0"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-13 21:15:49 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set gamma_id=2 spi1_div_ok=2 spi1_div_max=2 bl_pwm_hz=1000 bl_min_level=0 debounce_ms=20"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-13 21:15:50 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save force"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save force lines=2
- @R {"t": "hwcal_save", "seq": 1, "slot": "A"}

### 2026-11-13 21:16:01 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd reset`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK reset lines=1

### 2026-11-13 21:16:05 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "valid", "seq": 1, "slot": "A", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "gam

### 2026-11-13 21:16:26 — HWCAL written (manual note)

- First `hwcal save force` (fw of P3/P5 agent) failed: `flash write/verify failed`; sector 0xFFF000 afterwards unchanged (erase never took effect). Diagnostics added (step/st/SR dump, `w25q erase4k`, `w25q lock`). SR1/2/3 = 00/02/60, no protection.
- With the diagnostic build: `w25q erase4k FFF000 force` -> st=0, 86 ms, blank. Then `hwcal set ...` (two commands, 120-char line limit) + `hwcal save force` -> OK seq=1 slot A. `reset` + `hwcal get` -> status valid, all values persisted.
- Root cause of the first failure NOT reproduced (suspect state left by a preceding suite in that build; to watch). Saved values: land 0xA0 x1/y2, port 0x00 x2/y1, bgr 0, invert 0, gamma 2 (sitronix), spi1_div 2 (48 MHz), bl_pwm 1000 Hz, bl_min 0, debounce 20 ms.

### 2026-11-13 21:37:16 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 21:37:26 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 10.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:08.580
- Output: Time elapsed during verifying operation: 00:00:01.295
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:37:27 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin --wait-lock 900`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (181756 bytes), expected id: {'fw': '9337786+dirty', 'build': 1794594964}
- Programmer: exit code 0, verified=True
- App after start: running fw=9337786+dirty build=1794594964, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=10.32s, dfu_to_app=0.72s, total=11.72s

### 2026-11-13 21:37:31 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9337786+dirty build=1794594964 busy=idle

### 2026-11-13 21:37:34 — hwtest.py run [auto]

- Command: `hwtest.py run power.info --wait-lock 900`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-213731-power.info.json

### 2026-11-13 21:41:00 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd battlog start dry=1 --timeout 200 --wait-lock 900`
- Result: FAIL (exit code 2)
- COM10: status=timeout final=none (timeout) lines=93
- @R {"t": "battlog", "event": "start", "dry": true, "resume": false, "load_s": 20, "rest_s": 10, "records": 0}
- @R {"t": "bl", "n": 1, "t_s": 2, "vbat": 4206, "vcomp": 4206, "st": 15, "fl": 2, "ok": true}
- @R {"t": "bl", "n": 2, "t_s": 4, "vbat": 4190, "vcomp": 4190, "st": 15, "fl": 2, "ok": true}
- @R {"t": "bl", "n": 3, "t_s": 6, "vbat": 4183, "vcomp": 4183, "st": 15, "fl": 2, "ok": true}
- @R {"t": "bl", "n": 4, "t_s": 8, "vbat": 4195, "vcomp": 4195, "st": 15, "fl": 2, "ok": true}

### 2026-11-13 21:41:01 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd battlog status --wait-lock 900`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK battlog status lines=2
- @R {"t": "battlog_status", "running": false, "dry": true, "phase": "REST", "t_s": 120, "records": 90, "hdr": false, "ended": false, "flash_records": 0, "dry_records": 90, "write_errors": 0, "vbat_mv": 41

### 2026-11-13 21:41:59 — hwtest.py battlog [auto]

- Command: `hwtest.py battlog dump --wait-lock 900`
- Result: OK (exit code 0)
- battlog dump: 90 records -> C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\battlog-20261113-214159.csv ok=True

### 2026-11-13 21:43:48 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 21:43:58 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 10.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:08.493
- Output: Time elapsed during verifying operation: 00:00:01.269
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 21:43:59 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin --wait-lock 900`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (181780 bytes), expected id: {'fw': '9337786+dirty', 'build': 1794595397}
- Programmer: exit code 0, verified=True
- App after start: running fw=9337786+dirty build=1794595397, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=10.19s, dfu_to_app=0.71s, total=11.58s

### 2026-11-13 21:44:00 — Run suite power.handshake (start) [auto]

- Command: `run power.handshake t=3`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9337786+dirty build=1794595397 busy=idle

### 2026-11-13 21:44:03 — hwtest.py run [auto]

- Command: `hwtest.py run power.handshake t=3 --wait-lock 900`
- Result: FAIL (exit code 2)
- Suite power.handshake: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-214359-power.handshake.json

### 2026-11-13 21:44:35 — P2/P2b on hardware (manual note)

- power.info (USB in, charge done, SWD attached): VDDA 3308 mV, Vpin 3000 mV, Vbat 4207 mV (uncalibrated gain 1.0), die 30.3 C, CHG high, VBUS present, presence batt, batch noise 69 mV p-p, window spread 29 mV. PASS.
- battlog dry run (RAM, 120 s, S15 20 s / S2 10 s): 90 records, step flags OK, dump CRC OK, Vbat 4.18-4.21 V +-15 mV.
- No ammeter available (battery lead cannot be broken): currents will be estimated from voltage steps with the BQ24075 CC current (~174.5 mA) as reference (`power.rint`, battlog step records).

### 2026-11-13 21:50:15 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9337786+dirty build=1794595397 busy=idle

### 2026-11-13 21:50:17 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-215015-power.info.json

### 2026-11-13 21:50:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "adccal point 4175 3300"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK adccal point lines=2
- @R {"t": "adccal_point", "n": 1, "meter_vbat_mv": 4175, "meter_v3v3_mv": 3300, "unc_vbat_mv": 4175, "vdda_mv": 3293}

### 2026-11-13 21:50:19 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "adccal fit"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK adccal fit lines=2
- @R {"t": "adccal_fit", "ok": true, "points": 1, "gain_q16": 65536, "offset_mv": 0, "max_err_mv": 0, "vdda_err_mv": 7, "saved_to": "ram"}

### 2026-11-13 21:51:28 — Run suite power.handshake (start) [auto]

- Command: `run power.handshake ack=now t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9337786+dirty build=1794595397 busy=idle

### 2026-11-13 21:51:35 — ADC calibration point 1 (manual note)

- User multimeter (USB+ST-Link off): VBAT 4.16-4.17 V, 3V3 3.30 V; with USB back: VBAT 4.17-4.18 V, 3V3 3.30 V.
- `adccal point 4175 3300` (USB in, ST-Link off) -> firmware uncorrected Vbat 4175 mV = meter -> gain 1.000, offset 0 (divider 33k/82k accurate); VDDA via VREFINT 3293 mV vs 3300 meter (-7 mV, 0.2 %). No HWCAL change needed (defaults already gain 1.0 / offset 0).
- power.rint deferred to the battery cycle: at 4.17 V the BQ24075 is in CV (current < 175 mA), so the charger-step R_int would be overestimated.
- Starting power.handshake ack=now (ST-Link disconnected).

### 2026-11-13 21:53:34 — hwtest.py run [auto]

- Command: `hwtest.py run power.handshake ack=now t=300 --prompt-timeout 600`
- Result: FAIL (exit code 2)
- Suite power.handshake: DISCONNECTED results=1 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-215128-power.handshake.json

### 2026-11-13 21:55:36 — Run suite power.handshake (start) [auto]

- Command: `run power.handshake report`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9337786+dirty build=1794595397 busy=idle

### 2026-11-13 21:55:37 — hwtest.py run [auto]

- Command: `hwtest.py run power.handshake report`
- Result: OK (exit code 0)
- Suite power.handshake: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-215536-power.handshake.json

### 2026-11-13 21:56:15 — power.handshake ack=now (manual note)

- ST-Link disconnected, USB connected. OFF_REQ detect latency 1 us, debounce confirm 30-31 ms, ACK at 31 ms. Report after power-on: verdict `power_cut_right_after_ack`, `.noinit` crumb lost -> 3V3 really dropped (full power-off works with USB attached).
- **User observation: POWER had to be held ~4 s (not 0.5 s) before OFF_REQ was asserted; 2-3 s holds + release did nothing.** Hypothesis: ATtiny Timer0 prescaler mismatch (config.h comment says 64, default 8) -> all ATtiny intervals ~8x longer (0.5 s->4 s, ACK timeout 2.5 s->~20 s, hard reset 8 s->~64 s). Needs an ATtiny-side check (avr toolchain/USBasp not on this PC).

### 2026-11-13 22:09:46 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-13 22:09:47 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-13 22:09:58 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.1 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.223
- Output: Time elapsed during verifying operation: 00:00:01.464
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 22:09:59 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210944 bytes), expected id: {'fw': 'e1aa2f5+dirty', 'build': 1794596971}
- Programmer: exit code 0, verified=True
- App after start: running fw=e1aa2f5+dirty build=1794596971, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=11.13s, dfu_to_app=0.72s, total=12.53s

### 2026-11-13 22:10:11 — Run suite audio.cpu (start) [auto]

- Command: `run audio.cpu`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794596971 busy=idle

### 2026-11-13 22:10:17 — hwtest.py run [auto]

- Command: `hwtest.py run audio.cpu --timeout 60`
- Result: OK (exit code 0)
- Suite audio.cpu: PASS results=10 pass=5 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221011-audio.cpu.json

### 2026-11-13 22:10:40 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-13 22:10:52 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.1 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.212
- Output: Time elapsed during verifying operation: 00:00:01.464
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 22:10:52 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210944 bytes), expected id: {'fw': 'e1aa2f5+dirty', 'build': 1794597036}
- Programmer: exit code 0, verified=True
- App after start: running fw=e1aa2f5+dirty build=1794597036, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=11.12s, dfu_to_app=0.71s, total=12.52s

### 2026-11-13 22:10:53 — Run suite audio.cpu (start) [auto]

- Command: `run audio.cpu`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597036 busy=idle

### 2026-11-13 22:11:00 — hwtest.py run [auto]

- Command: `hwtest.py run audio.cpu --timeout 60`
- Result: OK (exit code 0)
- Suite audio.cpu: PASS results=10 pass=5 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221053-audio.cpu.json

### 2026-11-13 22:11:52 — Run suite audio.tone (start) [auto]

- Command: `run audio.tone f=1000 db=-24 s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597036 busy=idle

### 2026-11-13 22:12:52 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --answer-from-host --timeout 30`
- Result: FAIL (exit code 2)
- Suite audio.tone: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221152-audio.tone.json

### 2026-11-13 22:13:20 — Run suite audio.tone (start) [auto]

- Command: `run audio.tone f=1000 db=-24 s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597036 busy=idle

### 2026-11-13 22:14:20 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --answer-from-host --timeout 30`
- Result: FAIL (exit code 2)
- Suite audio.tone: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221320-audio.tone.json

### 2026-11-13 22:14:29 — Run suite audio.tone (start) [auto]

- Command: `run audio.tone f=1000 db=-24 s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597036 busy=idle

### 2026-11-13 22:14:35 — Board lock busy [auto]

- Command: `hwtest.py cmd "answer audio_tone_start ok"`
- Result: ERROR locked by pid 15176 ('hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60' since 2026-11-13T22:14:29+03:00)

### 2026-11-13 22:14:35 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "answer audio_tone_start ok"`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 15176 ('hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60' since 2026-11-13T22:14:29+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-13 22:14:54 — Board lock busy [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60 --answer-from-host`
- Result: ERROR locked by pid 15176 ('hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60' since 2026-11-13T22:14:29+03:00)

### 2026-11-13 22:14:54 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60 --answer-from-host`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 15176 ('hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60' since 2026-11-13T22:14:29+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-13 22:15:30 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60`
- Result: FAIL (exit code 2)
- Suite audio.tone: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221429-audio.tone.json

### 2026-11-13 22:15:41 — Run suite audio.tone (start) [auto]

- Command: `run audio.tone f=1000 db=-24 s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597036 busy=idle

### 2026-11-13 22:15:49 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60 --answer-from-host`
- Result: OK (exit code 0)
- Suite audio.tone: PASS results=3 pass=2 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221541-audio.tone.json

### 2026-11-13 22:16:08 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-13 22:16:19 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.250
- Output: Time elapsed during verifying operation: 00:00:01.467
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 22:16:20 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210936 bytes), expected id: {'fw': 'e1aa2f5+dirty', 'build': 1794597363}
- Programmer: exit code 0, verified=True
- App after start: running fw=e1aa2f5+dirty build=1794597363, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=11.15s, dfu_to_app=0.72s, total=12.56s

### 2026-11-13 22:16:30 — Run suite audio.tone (start) [auto]

- Command: `run audio.tone f=1000 db=-24 s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597363 busy=idle

### 2026-11-13 22:16:38 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 --timeout 60 --answer-from-host`
- Result: OK (exit code 0)
- Suite audio.tone: PASS results=3 pass=2 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221630-audio.tone.json

### 2026-11-13 22:16:38 — Run suite audio.tone (start) [auto]

- Command: `run audio.tone f=1000 db=-24 s=2 eng=a fs=22050`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597363 busy=idle

### 2026-11-13 22:16:46 — hwtest.py run [auto]

- Command: `hwtest.py run audio.tone f=1000 db=-24 s=2 eng=a fs=22050 --timeout 60 --answer-from-host`
- Result: OK (exit code 0)
- Suite audio.tone: PASS results=3 pass=2 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221638-audio.tone.json

### 2026-11-13 22:16:47 — Run suite audio.noise (start) [auto]

- Command: `run audio.noise db=-30 s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e1aa2f5+dirty build=1794597363 busy=idle

### 2026-11-13 22:16:55 — hwtest.py run [auto]

- Command: `hwtest.py run audio.noise db=-30 s=2 --timeout 60 --answer-from-host`
- Result: OK (exit code 0)
- Suite audio.noise: PASS results=3 pass=2 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261113-221647-audio.noise.json

### 2026-11-13 22:22:56 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-13 22:23:07 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.275
- Output: Time elapsed during verifying operation: 00:00:01.465
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-13 22:23:08 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210936 bytes), expected id: {'fw': 'e1aa2f5+dirty', 'build': 1794597745}
- Programmer: exit code 0, verified=True
- App after start: running fw=e1aa2f5+dirty build=1794597745, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=11.18s, dfu_to_app=0.71s, total=12.57s

### 2026-11-13 22:23:08 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=e1aa2f5+dirty build=1794597745 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-16 14:59:31 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 1.10 s
- cmd: @OK dfu

### 2026-11-16 14:59:42 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.234
- Output: Time elapsed during verifying operation: 00:00:01.465
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-16 14:59:43 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210928 bytes), expected id: {'fw': '9530a5e', 'build': 1794830362}
- Programmer: exit code 0, verified=True
- App after start: running fw=9530a5e build=1794830362, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=1.10s, program=11.25s, dfu_to_app=0.69s, total=13.15s

### 2026-11-16 15:01:19 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-16 15:01:30 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.248
- Output: Time elapsed during verifying operation: 00:00:01.479
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-16 15:01:30 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210952 bytes), expected id: {'fw': '9530a5e+dirty', 'build': 1794830474}
- Programmer: exit code 0, verified=True
- App after start: running fw=9530a5e+dirty build=1794830474, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=11.17s, dfu_to_app=0.71s, total=12.56s

### 2026-11-16 15:18:10 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=9530a5e+dirty build=1794830474 clk=96000000 busy=idle reset=por uid=002800343435511331323538

### 2026-11-16 15:18:10 — power.current attempt 1 (manual note)

- User: S1 (Stop) showed 0.00 on the meter 2 mA range, screen stayed dark >1 min; POWER press lit the screen and the board switched off immediately.
- Interpretation: switching the meter range (no jumper) opened the battery circuit -> full power loss -> ATtiny reset into OFF (deep-off current < 1 uA on the 2 mA range). POWER press booted the board, but the 2 mA-range shunt (~100 ohm burden) collapsed the supply -> off again. Not a firmware fault.
- Plan: keep the meter on the 200 mA range for the whole suite (no range switching). Added S10 (amp on, silent) and S11 (1 kHz -6 dBFS) states to power.current (loads.c).

### 2026-11-16 15:31:42 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: none
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-16 15:40:52 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.64 s
- cmd: @OK dfu

### 2026-11-16 15:41:04 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.242
- Output: Time elapsed during verifying operation: 00:00:01.476
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-16 15:41:04 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (210952 bytes), expected id: {'fw': '9530a5e+dirty', 'build': 1794832797}
- Programmer: exit code 0, verified=True
- App after start: running fw=9530a5e+dirty build=1794832797, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.64s, program=11.18s, dfu_to_app=0.70s, total=12.60s

### 2026-11-16 15:41:10 — Run suite power.current (start) [auto]

- Command: `run power.current s=1 n=5`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794832797 busy=idle

### 2026-11-16 15:41:28 — hwtest.py run [auto]

- Command: `hwtest.py run power.current s=1 n=5 --prompt-timeout 300`
- Result: FAIL (exit code 2)
- Suite power.current: DISCONNECTED results=0 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261116-154110-power.current.json

### 2026-11-16 15:41:56 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-16 15:42:17 — Stop-mode RTC wake bug fixed (manual note)

- Symptom: power.current S1 (Stop, 20 s RTC wake) never woke (>1-2 min dark); SWD hot-plug could not connect -> CPU really in Stop (not hung after wake).
- Root cause: `lowpower.c` cleared WUTF with `RTC->ISR = ~RTC_ISR_WUTF | (ISR & INIT)`; `~WUTF` also sets INIT=1 -> RTC enters init mode, prescalers/wakeup timer stop. Fixed to `~(WUTF|INIT) | (ISR & INIT)` (as HAL), in wut_arm() and RTC_WKUP_IRQHandler. LSI now always (re)started in lp_rtc_start (LSION is cleared by system reset, BDCR is not).
- Verify: power.current s=1 n=5 with USB in: USB detached in Stop and re-enumerated after the timer wake -> PASS. (Also affects battlog cutoff Stop and its RTC timestamps.)
- Measurement so far: S1 Stop current = 1.3-1.4 mA (battery, 200 mA range). Meter red lead had been in the 10 A jack before (mA ranges open) - user error, fixed.

### 2026-11-16 16:00:19 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794832797 busy=idle

### 2026-11-16 16:00:22 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261116-160019-power.info.json

### 2026-11-16 16:00:22 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 1 1.4"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 1, "ma_x10": 14, "i_state_ma": 2, "saved_to": "ram"}

### 2026-11-16 16:00:23 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 2 10.1"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 2, "ma_x10": 101, "i_state_ma": 11, "saved_to": "ram"}

### 2026-11-16 16:00:23 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 3 13.0"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 3, "ma_x10": 130, "i_state_ma": 13, "saved_to": "ram"}

### 2026-11-16 16:00:24 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 4 17.4"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 4, "ma_x10": 174, "i_state_ma": 18, "saved_to": "ram"}

### 2026-11-16 16:00:24 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 5 24.8"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 5, "ma_x10": 248, "i_state_ma": 25, "saved_to": "ram"}

### 2026-11-16 16:00:25 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 6 32.4"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 6, "ma_x10": 324, "i_state_ma": 33, "saved_to": "ram"}

### 2026-11-16 16:00:25 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 7 40.1"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 7, "ma_x10": 401, "i_state_ma": 41, "saved_to": "ram"}

### 2026-11-16 16:00:25 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 8 18.8"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 8, "ma_x10": 188, "i_state_ma": 19, "saved_to": "ram"}

### 2026-11-16 16:00:26 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 9 29.7"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 9, "ma_x10": 297, "i_state_ma": 30, "saved_to": "ram"}

### 2026-11-16 16:00:26 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "valid", "seq": 1, "slot": "A", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "gam

### 2026-11-16 16:04:23 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-16 16:04:34 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.246
- Output: Time elapsed during verifying operation: 00:00:01.476
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-16 16:04:35 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (211452 bytes), expected id: {'fw': '9530a5e+dirty', 'build': 1794834256}
- Programmer: exit code 0, verified=True
- App after start: running fw=9530a5e+dirty build=1794834256, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=11.17s, dfu_to_app=0.70s, total=12.57s

### 2026-11-16 16:04:49 — Run suite power.current (start) [auto]

- Command: `run power.current entry=0`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794834256 busy=idle

### 2026-11-16 16:05:42 — hwtest.py run [auto]

- Command: `hwtest.py run power.current entry=0 --prompt-timeout 900`
- Result: FAIL (exit code 2)
- Suite power.current: DISCONNECTED results=0 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261116-160449-power.current.json

### 2026-11-16 16:15:01 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794834256 busy=idle

### 2026-11-16 16:15:04 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261116-161501-power.info.json

### 2026-11-16 16:15:04 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 1 1.3"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 1, "ma_x10": 13, "i_state_ma": 2, "saved_to": "ram"}

### 2026-11-16 16:15:05 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 2 10.1"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 2, "ma_x10": 101, "i_state_ma": 11, "saved_to": "ram"}

### 2026-11-16 16:15:05 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 3 13.05"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 3, "ma_x10": 131, "i_state_ma": 14, "saved_to": "ram"}

### 2026-11-16 16:15:06 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 4 17.45"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 4, "ma_x10": 175, "i_state_ma": 18, "saved_to": "ram"}

### 2026-11-16 16:15:06 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 5 24.95"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 5, "ma_x10": 250, "i_state_ma": 25, "saved_to": "ram"}

### 2026-11-16 16:15:07 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 6 32.5"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 6, "ma_x10": 325, "i_state_ma": 33, "saved_to": "ram"}

### 2026-11-16 16:15:07 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 7 40.25"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 7, "ma_x10": 403, "i_state_ma": 41, "saved_to": "ram"}

### 2026-11-16 16:15:08 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 8 18.8"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 8, "ma_x10": 188, "i_state_ma": 19, "saved_to": "ram"}

### 2026-11-16 16:15:08 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 9 29.7"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 9, "ma_x10": 297, "i_state_ma": 30, "saved_to": "ram"}

### 2026-11-16 16:15:08 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 13 22.1"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 13, "ma_x10": 221, "i_state_ma": 23, "saved_to": "ram"}

### 2026-11-16 16:15:09 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 14 23.55"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 14, "ma_x10": 236, "i_state_ma": 24, "saved_to": "ram"}

### 2026-11-16 16:15:09 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur 15 31.6"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur lines=2
- @R {"t": "cur", "state": 15, "ma_x10": 316, "i_state_ma": 32, "saved_to": "ram"}

### 2026-11-16 16:15:10 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "valid", "seq": 1, "slot": "A", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "gam

### 2026-11-16 16:15:10 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "cur list"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK cur list lines=13
- @R {"t": "cur", "state": 1, "ma_x10": 13, "vbat_loaded_mv": 3840, "temp_dC": 277}
- @R {"t": "cur", "state": 2, "ma_x10": 101, "vbat_loaded_mv": 4087, "temp_dC": 279}
- @R {"t": "cur", "state": 3, "ma_x10": 131, "vbat_loaded_mv": 4011, "temp_dC": 283}
- @R {"t": "cur", "state": 4, "ma_x10": 175, "vbat_loaded_mv": 3896, "temp_dC": 253}
- @R {"t": "cur", "state": 5, "ma_x10": 250, "vbat_loaded_mv": 3969, "temp_dC": 261}

### 2026-11-16 16:15:31 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save lines=2
- @R {"t": "hwcal_save", "seq": 2, "slot": "B"}

### 2026-11-16 16:16:07 — power.current results (manual note)

- Meter (200 mA range, red lead in mA jack) in series with the battery, USB out, ST-Link out. Suite run with `entry=0` (values typed on host via `cur`), loaded Vbat auto-logged per state (`cur list`). Vbat at the board includes the meter burden (~7-8 ohm equivalent incl. fuse/leads: 4.087 V @10 mA -> 3.854 V @40 mA); P = I x V_board is the system input power.
- mA / V_board / mW: S1 Stop 1.3/3.84/5.0; S2 idle BL off 10.1/4.09/41.3; S3-S7 BL 10/25/50/75/100%: 13.05/17.45/24.95/32.5/40.25 mA = 52/68/99/126/155 mW; S8 CPU max 18.8 mA 76.5 mW; S9 LCD DMA max (BL50) 29.7 mA 116 mW; S13 W25Q read 22.1 mA 89 mW; S14 W25Q erase/prog 23.55 mA 94 mW; S15 game mix 31.6 mA 124 mW. S10/S11 (amp) skipped: with the meter in series, enabling the amp browned out the ATtiny (+V) -> board powered off (F6). S16 meaningless now (battery full, no charge current).
- Estimate: 1.15 Wh / 124 mW = ~9 h game-mix runtime (to be confirmed by battlog). i_state_ma stored, `hwcal save` seq 2 slot B.
- Firmware: power.current gained S10/S11 amp states, auto Vbat logging + `cur list`, `entry=0`.

### 2026-11-16 16:18:03 — Run suite power.current (start) [auto]

- Command: `run power.current s=5 n=600 entry=0`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794834256 busy=idle

### 2026-11-16 16:19:03 — hwtest.py run [auto]

- Command: `hwtest.py run power.current s=5 n=600 entry=0 --prompt-timeout 600`
- Result: FAIL (exit code 2)
- Suite power.current: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261116-161803-power.current.json

### 2026-11-16 16:19:31 — Run suite power.current (start) [auto]

- Command: `run power.current s=5 n=600 entry=0 t=600 step=600`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794834256 busy=idle

### 2026-11-16 16:26:17 — hwtest.py run [auto]

- Command: `hwtest.py run power.current s=5 n=600 entry=0 t=600 step=600 --prompt-timeout 900`
- Result: FAIL (exit code 2)
- Suite power.current: DISCONNECTED results=0 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261116-161931-power.current.json

### 2026-11-16 16:26:36 — hwtest.py run [auto]

- Command: `hwtest.py run power.current s=5 n=600 entry=0 t=600 step=600`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-16 16:35:54 — Run suite power.current (start) [auto]

- Command: `run power.current s=5 n=600 entry=0 t=600 step=600`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794834256 busy=idle

### 2026-11-16 16:41:52 — Scope: backlight PWM (manual note)

- Tektronix TDS3032B, CH1 x10 at R27 (MCU side, PA6), board on USB, power.current S5 (BL 50%, HWCAL 1 kHz).
- Result: **Freq 1.000 kHz, +Duty 49.99 %** -> PWM correct; firmware value 47.4 % was an IDR-sampling artefact. F3 closed. (Trace looked AC-coupled / probe uncompensated - set DC coupling + probe comp for next measurements.)

### 2026-11-16 16:42:10 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd abort`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK abort lines=1

### 2026-11-16 16:42:16 — Run suite power.current (start) [auto]

- Command: `run power.current s=9 n=600 entry=0 t=600 step=600`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9530a5e+dirty build=1794834256 busy=idle

### 2026-11-16 16:50:04 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd abort`
- Result: FAIL (exit code 2)
- COM10: status=done final=@DONE power.current abort lines=3
- @R {"t": "skip", "suite": "power.current", "reason": "abort"}
- SPI1 SCK at 48 MHz: not measured (FPC contacts hidden under the panel, MCU pins too small to probe). Integrity already covered by display.spi visual stress test (stable at 48 MHz).

### 2026-11-16 16:56:47 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-16 16:56:58 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.263
- Output: Time elapsed during verifying operation: 00:00:01.478
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-16 16:56:59 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (211556 bytes), expected id: {'fw': 'e38a1ed+dirty', 'build': 1794837402}
- Programmer: exit code 0, verified=True
- App after start: running fw=e38a1ed+dirty build=1794837402, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=11.19s, dfu_to_app=0.69s, total=12.58s

### 2026-11-16 16:57:15 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "battlog start dry=1 ls=12 load=20 rest=10" --timeout 15`
- Result: FAIL (exit code 2)
- COM10: status=timeout final=none (timeout) lines=8
- @R {"t": "battlog", "event": "start", "dry": true, "resume": false, "load_s": 20, "rest_s": 10, "records": 0}
- @R {"t": "bl", "n": 1, "t_s": 2, "vbat": 4176, "vcomp": 4176, "st": 12, "fl": 2, "ok": true}
- @R {"t": "bl", "n": 2, "t_s": 4, "vbat": 4189, "vcomp": 4189, "st": 12, "fl": 2, "ok": true}
- @R {"t": "bl", "n": 3, "t_s": 6, "vbat": 4192, "vcomp": 4192, "st": 12, "fl": 2, "ok": true}
- @R {"t": "bl", "n": 4, "t_s": 8, "vbat": 4202, "vcomp": 4202, "st": 12, "fl": 2, "ok": true}

### 2026-11-16 16:57:22 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "battlog stop" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK battlog stop lines=1

### 2026-11-16 16:59:03 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "battlog start new=1 ls=12" --timeout 8`
- Result: FAIL (exit code 2)
- COM10: status=timeout final=none (timeout) lines=2
- @R {"t": "battlog", "event": "start", "dry": false, "resume": false, "load_s": 540, "rest_s": 60, "records": 0}
- @R {"t": "bl", "n": 1, "t_s": 5, "vbat": 4189, "vcomp": 4189, "st": 12, "fl": 2, "ok": true}

### 2026-11-16 16:59:09 — Battery cycle started (manual note)

- `battlog start new=1 ls=12` (load phase = new S12 max load: BL100 + LCD DMA + CPU, ~230 mW est.; 9 min load / 1 min rest + 15-min rests at ~75/50/25/10 %). Meter removed, ST-Link removed, battery full (first record 4.189 V with USB). User unplugs USB now; board runs autonomously, cutoff 3.30 V loaded -> Stop -> charge phase logged on VBUS. Expected discharge ~5-5.5 h.

### 2026-11-17 07:49:20 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: none
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-17 07:50:44 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "battlog status" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK battlog status lines=2
- @R {"t": "battlog_status", "running": true, "dry": false, "phase": "CHARGE", "t_s": 53522, "records": 9696, "hdr": false, "ended": false, "flash_records": 0, "dry_records": 14, "write_errors": 0, "vbat_m

### 2026-11-17 07:51:20 — hwtest.py battlog [auto]

- Command: `hwtest.py battlog dump --src flash`
- Result: FAIL (exit code 1)

### 2026-11-17 07:51:57 — Battery cycle status (manual note)

- Discharge (ls=12) lasted ~9 h per user (longer than the 5.5 h estimate -> S12 power lower than assumed). User check at ~01:00: multimeter 3.47 V at J2 vs console Vbat 3460-3480 mV (around record #8272) -> ADC 2nd point agrees within ~10 mV, no correction needed.
- Charger plugged ~40 min before; screen flashed white then went dark = expected (`enter_phase(PH_CHARGE)` -> `load_end()` sets full backlight, then S2 rest turns it off).
- Moved USB from wall charger to the laptop. `battlog status`: running, phase CHARGE, t=53522 s, 9696 records, write_errors 0, Vbat 3666 mV (CC charge). `battlog dump` refused while running (busy) -> waiting for the logger to finish (CHG high + 30 min).

### 2026-11-17 08:02:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "battlog status" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK battlog status lines=2
- @R {"t": "battlog_status", "running": false, "dry": false, "phase": "CHARGE", "t_s": 53567, "records": 9702, "hdr": true, "ended": true, "flash_records": 9702, "dry_records": 14, "write_errors": 0, "vbat

### 2026-11-17 08:02:42 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e38a1ed+dirty build=1794837402 busy=idle

### 2026-11-17 08:02:44 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-080241-power.info.json

### 2026-11-17 08:03:09 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=e38a1ed+dirty build=1794837402 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-17 08:03:33 — hwtest.py battlog [auto]

- Command: `hwtest.py battlog dump --src flash`
- Result: OK (exit code 0)
- battlog dump: 9702 records -> C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\battlog-20261117-080333.csv ok=True

### 2026-11-17 08:04:16 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd reset`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK reset lines=1

### 2026-11-17 08:04:23 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=e38a1ed+dirty build=1794837402 busy=idle

### 2026-11-17 08:04:26 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-080423-power.info.json

### 2026-11-17 08:04:52 — Battery cycle dump + VBUS problem (manual note)

- The logger stopped at t=53567 s (~45 s after the 05:xx status): my `hwtest.py battlog dump` attempt got `@ERR battlog busy` and the host error path apparently sent `abort` -> charge-phase logging stopped after ~50 min of charge. HOST BUG to fix (dump must not abort a running log). The 5-min watcher also died immediately (wrong `--no-log` placement in my script).
- `battlog dump --src flash`: 9702 records OK -> tools/results/.../battlog-20261117-080333.bin/.csv; `battlog_analyze.py` -> discharge (ls=12) 9:18:32 to the 3.30 V loaded cutoff; Stop gap 4:42; OCV table 3320..4161 mV (21 pts); R_int steps +1 s median 291 mOhm, +10 s 376 mOhm; charger plug step 3290->3428 mV.
- **Problem:** on the laptop the board reports vbus=false, chg=false, Vbat falling (3666->3610 mV) although USB CDC works; same after soft reset -> no +5V on the board rail from this laptop port (wall charger last night did deliver VBUS). Also CHG (#CHG, PA0) never showed "charging" in the log during the night charge (analyzer: CC n/a). To check with multimeter at +5V TP, another port/cable.

### 2026-11-17 08:12:32 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-17 08:13:04 — F8 confirmed (manual note)

- +5V TP with no cable: 2.41 V (board back-feeds VBUS). With a USB-A to C cable (charge-only, no data) +5V = 5 V and charging works. => USB-C sources that check vSafe0V (laptop C-C) refuse to power the board. Workaround: A-C cable / wall charger. Charge curve: only the first ~50 min captured (host abort bug); full charge curve to be re-captured later.
- power.info on device (A-C cable, ~3.6 V): CHG low (charging), VBUS yes -> #CHG hardware OK; F9 = battlog firmware issue.
- Correction (user): the charger was NOT connected overnight; it was plugged at 07:00. Timeline: start ~17:00, cutoff ~02:17 (9:18), Stop 4:42, wall charger 07:00-07:48 (~50 min CC charge, CHG flag never set in the log), then laptop C-C (no VBUS, F8), logger aborted at ~07:52 by the host dump attempt.

### 2026-11-17 12:32:24 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: FAIL (exit code 2)

### 2026-11-17 12:33:19 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-17 12:33:36 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-17 12:33:47 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.282
- Output: Time elapsed during verifying operation: 00:00:01.479
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 12:33:48 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (211828 bytes), expected id: {'fw': 'af00cc4+dirty', 'build': 1794906519}
- Programmer: exit code 0, verified=True
- App after start: running fw=af00cc4+dirty build=1794906519, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=11.31s, dfu_to_app=0.70s, total=12.70s

### 2026-11-17 12:33:54 — Run suite power.current (start) [auto]

- Command: `run power.current s=9 n=900 entry=0 t=600 step=900`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=af00cc4+dirty build=1794906519 busy=idle

### 2026-11-17 12:43:54 — hwtest.py run [auto]

- Command: `hwtest.py run power.current s=9 n=900 entry=0 t=600 step=900 --prompt-timeout 1200`
- Result: FAIL (exit code 2)
- Suite power.current: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-123354-power.current.json

### 2026-11-17 12:50:46 — Scope: SPI1 SCK (PA5, pin 15) at 48 MHz (manual note)

- TDS3032B, CH1 x10 on a wire soldered to PA5, stream = power.current S9 (LCD DMA continuous, div 2), prompts auto-answered by a host script.
- Attempt 1 (long ground clip): Freq 48.01 MHz, +Duty 48.12 %, Rise 7.49 ns, Pk-Pk 6.44 V (about -1.4..+4.8 V) with strong ringing and step-like reflections on the edges -> beyond the 3.3 V rails, i.e. measurement-loop ringing (wire + long ground lead), not the pin voltage. Asked user to shorten the ground (spring / wrapped wire to the nearest GND) and the wire.
- Firmware: new command `lcd div <2|4|8|16|32>` to change the SPI1 prescaler on the fly.

### 2026-11-17 13:01:27 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "lcd div 4" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK lcd div lines=2
- @R {"t": "lcd", "spi_div": 4, "spi_hz": 24000000}

### 2026-11-17 13:03:53 — Enter DFU from application (touch) [auto]

- Command: `1200-baud touch on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- touch: exception (expected): Cannot configure port, something went wrong. Original message: OSError(22, 'A device which does not exist was specified.', None, 433)

### 2026-11-17 13:04:04 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.292
- Output: Time elapsed during verifying operation: 00:00:01.478
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 13:04:05 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (211884 bytes), expected id: {'fw': 'af00cc4+dirty', 'build': 1794909829}
- Programmer: exit code 0, verified=True
- App after start: running fw=af00cc4+dirty build=1794909829, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=11.22s, dfu_to_app=0.69s, total=12.61s

### 2026-11-17 13:04:42 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "lcd speed 1" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK lcd speed lines=2
- @R {"t": "lcd", "gpio_speed": 1}

### 2026-11-17 13:05:49 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "lcd speed 0" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK lcd speed lines=2
- @R {"t": "lcd", "gpio_speed": 0}

### 2026-11-17 13:08:31 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "lcd div 8" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK lcd div lines=2
- @R {"t": "lcd", "spi_div": 8, "spi_hz": 12000000}

### 2026-11-17 13:11:00 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd abort --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK abort lines=1

### 2026-11-17 13:11:01 — Scope: SPI1 SCK results (manual note)

- 48 MHz, speed 3 (very high), short-ish ground: unchanged (Pk-Pk ~6.4 V, ringing ~100-120 MHz).
- 24 MHz, speed 3: 24.01 MHz, duty 48.9 %, damped ringing after every edge (~8-10 ns period), same Pk-Pk 6.7 V -> ringing amplitude independent of SPI clock; a -1.6 V undershoot AT the MCU pin is impossible with the pin clamp diodes -> dominated by the probe/wire loop. Rise-time readings (7.5 / 18.9 ns) unreliable (10-90 % thresholds hit the ringing).
- 48 MHz, GPIO speed 1 (medium, new `lcd speed`): display OK, scope trace unchanged.
- 48 MHz, GPIO speed 0 (low): FAILS - SCK amplitude ~1 V, frequency reading unstable, display frozen on the last good frame.
- 12 MHz, GPIO speed 0: clean square, 11.97 MHz, duty 50.5 %, rise 11 ns, no ringing, Pk-Pk 3.80 V (probe scale reads ~15 % high).
- Conclusion: 48 MHz needs OSPEEDR >= medium; recommend 48 MHz + high (2) for margin; v0.2: 22-33 ohm series resistors on SCK/MOSI near the MCU.

### 2026-11-17 13:39:28 — ATtiny13 reflashed (manual note)

- User reflashed the ATtiny13 with the Timer0 prescaler fix (commit e1aa2f5) using their own programmer. Expected now: POWER hold ~0.5 s -> OFF_REQ, OFF_ACK timeout 2.5 s, hard reset at 8 s hold. Verification pending.

### 2026-11-17 13:39:29 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-17 13:42:42 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-17 13:42:54 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.267
- Output: Time elapsed during verifying operation: 00:00:01.477
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 13:42:54 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (212188 bytes), expected id: {'fw': '23c2510+dirty', 'build': 1794912148}
- Programmer: exit code 0, verified=True
- App after start: running fw=23c2510+dirty build=1794912148, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.63s, program=11.19s, dfu_to_app=0.71s, total=12.61s

### 2026-11-17 13:42:55 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "valid", "seq": 2, "slot": "B", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "gam

### 2026-11-17 13:43:06 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set rint_mohm=330 i_chg_ma=175"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-17 13:43:07 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal ocv 0 3320,3475,3505,3541,3568,3589,3614,3647,3667,3688,3741,3781,3826,3865,3899,3954,3994,4009,4060,4110,4161"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal ocv lines=1

### 2026-11-17 13:43:07 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR hwcal flash write/verify failed: step=erase_verify st=0 sr1=02 sr2=02 sr3=60 lines=1

### 2026-11-17 13:43:08 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd reset`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK reset lines=1

### 2026-11-17 13:43:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal ocv 0 3320,3475,3505,3541,3568,3589,3614,3647,3667,3688,3741,3781,3826,3865,3899,3954,3994,4009,4060,4110,4161"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal ocv lines=1

### 2026-11-17 13:43:19 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set rint_mohm=330 i_chg_ma=175"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-17 13:43:20 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR hwcal flash write/verify failed: step=erase_verify st=0 sr1=02 sr2=02 sr3=60 lines=1

### 2026-11-17 13:43:58 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q id"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q id lines=2
- @R {"t": "w25q_id", "jedec": "EF4018", "uid": "E463A8F31B121931", "sr1": "02", "spi_hz": 12000000}

### 2026-11-17 13:43:59 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan FFF000 4096 256"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "FFF000", "len": 4096, "step": 256, "granules": 16, "blank": 14, "used": 2, "nonff_bytes": 264, "first_nonff": "FFF000", "last_nonff": "FFF883", "ranges": [["FFF000", "FFF10

### 2026-11-17 13:44:28 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q erase4k FFF000 force"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q erase4k lines=2
- @R {"t": "w25q_erase4k", "addr": "FFF000", "st": 0, "st_s": "ok", "sr1_before": "02", "sr1": "00", "us": 89132, "first16_blank": true}

### 2026-11-17 13:44:37 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save lines=2
- @R {"t": "hwcal_save", "seq": 3, "slot": "A"}

### 2026-11-17 13:44:37 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd reset`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK reset lines=1

### 2026-11-17 13:44:42 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "valid", "seq": 3, "slot": "A", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "gam

### 2026-11-17 13:44:48 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd power.info`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR power.info unknown command lines=1

### 2026-11-17 13:45:07 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=23c2510+dirty build=1794912148 busy=idle

### 2026-11-17 13:45:09 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: OK (exit code 0)
- Suite power.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-134506-power.info.json

### 2026-11-17 13:48:53 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-17 13:49:04 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.393
- Output: Time elapsed during verifying operation: 00:00:01.493
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 13:49:05 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (214932 bytes), expected id: {'fw': '23c2510+dirty', 'build': 1794912507}
- Programmer: exit code 0, verified=True
- App after start: running fw=23c2510+dirty build=1794912507, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.63s, program=11.36s, dfu_to_app=0.70s, total=12.77s

### 2026-11-17 13:49:05 — Run suite display.tear auto=1 (start) [auto]

- Command: `run display.tear auto=1`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=23c2510+dirty build=1794912507 busy=idle

### 2026-11-17 13:49:24 — hwtest.py run [auto]

- Command: `hwtest.py run "display.tear auto=1"`
- Result: OK (exit code 0)
- Suite display.tear auto=1: PASS results=7 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-134905-display.tear_auto_1.json

### 2026-11-17 13:51:33 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-17 13:51:44 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.424
- Output: Time elapsed during verifying operation: 00:00:01.534
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 13:51:45 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (220476 bytes), expected id: {'fw': '23c2510+dirty', 'build': 1794912681}
- Programmer: exit code 0, verified=True
- App after start: running fw=23c2510+dirty build=1794912681, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.63s, program=11.41s, dfu_to_app=0.71s, total=12.83s

### 2026-11-17 13:51:49 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd gauge`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK gauge lines=2
- @R {"t": "gauge", "mode": "CHARGING", "level": 4, "valid": true, "flags": 12, "soc": 97.8, "vcomp_mv": 4139, "vbat_mv": 4199, "load_ma": 38, "load_state": 7, "vbus": true, "chg": true, "drops": 0, "rises

### 2026-11-17 13:51:54 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd gauge`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK gauge lines=2
- @R {"t": "gauge", "mode": "CHARGING", "level": 4, "valid": true, "flags": 12, "soc": 97.9, "vcomp_mv": 4140, "vbat_mv": 4199, "load_ma": 38, "load_state": 7, "vbus": true, "chg": true, "drops": 0, "rises

### 2026-11-17 13:54:42 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-17 13:54:53 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.496
- Output: Time elapsed during verifying operation: 00:00:01.533
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 13:54:54 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (220476 bytes), expected id: {'fw': '23c2510+dirty', 'build': 1794912849}
- Programmer: exit code 0, verified=True
- App after start: running fw=23c2510+dirty build=1794912849, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=11.50s, dfu_to_app=0.70s, total=12.88s

### 2026-11-17 13:54:55 — Run suite display.tear auto=1 t=1 (start) [auto]

- Command: `run display.tear auto=1 t=1`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=23c2510+dirty build=1794912849 busy=idle

### 2026-11-17 13:55:02 — hwtest.py run [auto]

- Command: `hwtest.py run "display.tear auto=1 t=1"`
- Result: OK (exit code 0)
- Suite display.tear auto=1 t=1: PASS results=7 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-135455-display.tear_auto_1_t_1.json

### 2026-11-17 13:55:58 — Code block: host abort fix, CHG re-sense, HWCAL battery, display.tear, gauge (manual note)

- hwtest.py sends `abort` only for suites it started; `battlog busy` handled.
- HWCAL now holds the measured OCV table, rint 330 mOhm, i_chg 175 mA (seq 3 slot A; first `hwcal save` failed twice with erase_verify, F4 recurred, worked after `w25q erase4k FFF000 force`).
- display.tear auto (48 MHz DMA): land MV 30.2/60.1/129 fps, native (MADCTL 0x40, column-major frame) 30.2/60.1/128 fps. Rotation 160x128 RGB565 ~1.31 ms; 8bpp->565 rotated ~2.15 ms. Native orientation/mirroring not yet checked by eye.
- Gauge in firmware (status-bar 5-segment icon); replay of the real discharge log: 0 reversals, drops at SoC 81/61/42/22/2.7 %%.
- F9 closed (CHG was logged; analyzer bug). Board on laptop: power.info chg=true vbus=true Vbat 4.207 V.

### 2026-11-17 14:11:59 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-17 14:12:11 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 11.5 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.489
- Output: Time elapsed during verifying operation: 00:00:01.534
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 14:12:12 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (220476 bytes), expected id: {'fw': '815965a+dirty', 'build': 1794913914}
- Programmer: exit code 0, verified=True
- App after start: running fw=815965a+dirty build=1794913914, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=11.47s, dfu_to_app=0.71s, total=12.86s

### 2026-11-17 14:25:21 — display.tear by eye (manual note)

- Fixed: native mode MADCTL 0x40 showed the picture rotated 180 deg -> 0x80 (MY) is correct (user confirmed). Status bar text overlapped the battery icon -> shortened (user confirmed OK). Yellow icon = charging plug.
- LAND MV: strong diagonal tearing; best at panel 60.2 Hz (FRMCTR1 preset, computed 60.25 Hz) with 30/60 fps, still a diagonal seam drifting right->left and repeating (~4 s beat 60.0 vs 60.25 Hz).
- NATIVE: no visible change between fps/panel-rate combinations; tear practically invisible; some ghosting/double image of the moving bar = TN LC response.
- Recommendation: native-order transfer (MADCTL 0x80 + rotated frame) at 30/60 fps.

### 2026-11-17 14:45:38 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-17 14:45:50 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.879
- Output: Time elapsed during verifying operation: 00:00:01.646
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 14:45:50 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (237124 bytes), expected id: {'fw': '3dffea5+dirty', 'build': 1794915914}
- Programmer: exit code 0, verified=True
- App after start: running fw=3dffea5+dirty build=1794915914, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=11.97s, dfu_to_app=0.71s, total=13.39s

### 2026-11-17 14:45:51 — Run suite display.native (start) [auto]

- Command: `run display.native`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3dffea5+dirty build=1794915914 busy=idle

### 2026-11-17 14:45:54 — hwtest.py run [auto]

- Command: `hwtest.py run display.native`
- Result: OK (exit code 0)
- Suite display.native: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-144551-display.native.json

### 2026-11-17 14:46:03 — Run suite demo (start) [auto]

- Command: `run demo auto=1`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3dffea5+dirty build=1794915914 busy=idle

### 2026-11-17 14:46:19 — hwtest.py run [auto]

- Command: `hwtest.py run demo auto=1`
- Result: OK (exit code 0)
- Suite demo: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-144603-demo.json

### 2026-11-17 14:46:48 — Run suite display.vscroll (start) [auto]

- Command: `run display.vscroll auto=1`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3dffea5+dirty build=1794915914 busy=idle

### 2026-11-17 14:47:04 — hwtest.py run [auto]

- Command: `hwtest.py run display.vscroll auto=1`
- Result: OK (exit code 0)
- Suite display.vscroll: PASS results=4 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-144648-display.vscroll.json

### 2026-11-17 14:49:07 — hwtest.py run [auto]

- Command: `hwtest.py run display.info`
- Result: FAIL (exit code 2)

### 2026-11-17 14:49:08 — hwtest.py run [auto]

- Command: `hwtest.py run display.tear auto=1 t=1`
- Result: FAIL (exit code 2)

### 2026-11-17 14:49:09 — hwtest.py run [auto]

- Command: `hwtest.py run bench.lcd`
- Result: FAIL (exit code 2)

### 2026-11-17 14:49:15 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3dffea5+dirty build=1794915914 clk=96000000 busy=demo reset=sft uid=002800343435511331323538

### 2026-11-17 14:49:21 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3dffea5+dirty build=1794915914 clk=96000000 busy=demo reset=sft uid=002800343435511331323538

### 2026-11-17 14:49:22 — hwtest.py run [auto]

- Command: `hwtest.py run display.info`
- Result: FAIL (exit code 2)

### 2026-11-17 14:49:40 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd abort`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK abort lines=1

### 2026-11-17 14:49:43 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3dffea5+dirty build=1794915914 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-17 14:49:49 — Run suite display.info (start) [auto]

- Command: `run display.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3dffea5+dirty build=1794915914 busy=idle

### 2026-11-17 14:49:49 — hwtest.py run [auto]

- Command: `hwtest.py run display.info`
- Result: OK (exit code 0)
- Suite display.info: PASS results=1 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-144949-display.info.json

### 2026-11-17 14:49:50 — Run suite display.tear (start) [auto]

- Command: `run display.tear auto=1 t=1`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3dffea5+dirty build=1794915914 busy=idle

### 2026-11-17 14:49:57 — hwtest.py run [auto]

- Command: `hwtest.py run display.tear auto=1 t=1`
- Result: OK (exit code 0)
- Suite display.tear: PASS results=7 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-144950-display.tear.json

### 2026-11-17 14:49:58 — Run suite bench.lcd (start) [auto]

- Command: `run bench.lcd`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3dffea5+dirty build=1794915914 busy=idle

### 2026-11-17 14:50:05 — hwtest.py run [auto]

- Command: `hwtest.py run bench.lcd`
- Result: OK (exit code 0)
- Suite bench.lcd: PASS results=45 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-144958-bench.lcd.json

### 2026-11-17 — Native scan order for the whole UI + demo + display.vscroll (firmware note)

- lcd.c: native mode default (LCD_NATIVE_DEFAULT=1): API stays landscape, controller addressed MV=0 (phys MADCTL = logical & ~MV = 0x80), landscape x -> RASET row (+xoff 1), y -> CASET col (+yoff 2). `display.native` PASS: TL(col2,row1) BR(col129,row160). display.cal/orient/spi/tear and bench.lcd/gfx run in raw mode (lcd_set_native(false)). GPIO speed default 2.
- demo auto=1 (5 s/scene, 96 MHz, SPI 48 MHz): cubes 143.9 fps (R 3.1 ms, P-wait 3.7), raycaster 143.8 fps (R 5.1, P 1.7), raytracer 80x64x2 41.2 fps (R 24.1, P 0). Present-limited at ~6.9 ms/frame.
- display.vscroll auto=1: hw step 8.5k cyc (~89 us: gen 3.4k, column write 4.9k, VSCRSADD 0.2k) vs full redraw 1.24M cyc (12.9 ms: render 0.58M + DMA 0.66M). To be judged by eye: smoothness / direction / HUD stays fixed.

### 2026-11-17 15:16:36 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-17 15:16:48 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.867
- Output: Time elapsed during verifying operation: 00:00:01.673
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 15:16:49 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (239004 bytes), expected id: {'fw': '27475cd+dirty', 'build': 1794917777}
- Programmer: exit code 0, verified=True
- App after start: running fw=27475cd+dirty build=1794917777, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=11.99s, dfu_to_app=0.71s, total=13.38s

### 2026-11-17 15:16:59 — Run suite display.vscroll (start) [auto]

- Command: `run display.vscroll auto=1 ph=3 t=60`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=27475cd+dirty build=1794917777 busy=idle

### 2026-11-17 15:18:00 — hwtest.py run [auto]

- Command: `hwtest.py run display.vscroll auto=1 ph=3 t=60`
- Result: OK (exit code 0)
- Suite display.vscroll: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-151659-display.vscroll.json

### 2026-11-17 15:18:20 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-17 15:18:32 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.863
- Output: Time elapsed during verifying operation: 00:00:01.662
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 15:18:32 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (239020 bytes), expected id: {'fw': '27475cd+dirty', 'build': 1794917895}
- Programmer: exit code 0, verified=True
- App after start: running fw=27475cd+dirty build=1794917895, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=11.98s, dfu_to_app=0.70s, total=13.38s

### 2026-11-17 15:18:33 — Run suite display.vscroll (start) [auto]

- Command: `run display.vscroll auto=1 ph=3 t=60`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=27475cd+dirty build=1794917895 busy=idle

### 2026-11-17 15:19:33 — hwtest.py run [auto]

- Command: `hwtest.py run display.vscroll auto=1 ph=3 t=60`
- Result: OK (exit code 0)
- Suite display.vscroll: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-151833-display.vscroll.json

### 2026-11-17 15:19:50 — Run suite display.vscroll (start) [auto]

- Command: `run display.vscroll auto=1 t=3`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=27475cd+dirty build=1794917895 busy=idle

### 2026-11-17 15:20:02 — hwtest.py run [auto]

- Command: `hwtest.py run display.vscroll auto=1 t=3`
- Result: OK (exit code 0)
- Suite display.vscroll: PASS results=4 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-151950-display.vscroll.json

### 2026-11-17 15:20:03 — Run suite demo (start) [auto]

- Command: `run demo auto=1 t=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=27475cd+dirty build=1794917895 busy=idle

### 2026-11-17 15:20:09 — hwtest.py run [auto]

- Command: `hwtest.py run demo auto=1 t=2`
- Result: OK (exit code 0)
- Suite demo: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261117-152003-demo.json

### 2026-11-17 15:20:10 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd ping`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK ping lines=1

### 2026-11-17 15:20:11 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=27475cd+dirty build=1794917895 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-17 15:22:20 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-17 15:22:32 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:09.922
- Output: Time elapsed during verifying operation: 00:00:01.660
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-17 15:22:33 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (239020 bytes), expected id: {'fw': '27475cd+dirty', 'build': 1794918104}
- Programmer: exit code 0, verified=True
- App after start: running fw=27475cd+dirty build=1794918104, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=12.03s, dfu_to_app=0.69s, total=13.42s

### 2026-11-17 15:22:41 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd ping`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK ping lines=1

### 2026-11-17 (manual note) vscroll sw_max slowdown

- Cause: vs_gen() called sinf(W*0.12); newlib sinf leaves its fast range reduction once |x| > ~200 (W > ~1700, ~25 s at 67 fps) and gets much slower, so sw_max drifted from ~78 fps to ~61 fps (win_min 61.5 / win_max 78.2 over 60 s). Fixed by reducing the phase in integer (W*1252 & 0xFFFF): sw_max now flat 79.2-79.4 fps over 60 s. Phases never auto-advance in manual mode.
- Menu/Tests list now compose into a 160x128 RGB565 frame in g_arena (lcd_capture) and DMA it in native order at 48 MHz; demo overlay palette 254/255 were never set (text colour undefined) -> now set + dark box.

### 2026-11-18 20:24:13 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-18 20:46:11 — ATtiny13 fix verified (manual note)

- User: after reflashing the Timer0 prescaler fix, POWER hold/power-off works correctly. F1 closed.

### 2026-11-18 20:49:34 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 1.06 s
- cmd: @OK dfu

### 2026-11-18 20:49:46 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.193
- Output: Time elapsed during verifying operation: 00:00:01.764
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 20:49:47 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (252472 bytes), expected id: {'fw': 'd8384c2+dirty', 'build': 1795024148}
- Programmer: exit code 0, verified=True
- App after start: running fw=d8384c2+dirty build=1795024148, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=1.06s, program=12.42s, dfu_to_app=0.71s, total=14.27s

### 2026-11-18 20:49:48 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2
- @R {"t": "clk", "sysclk": 96000000, "hclk": 96000000, "pclk1": 48000000, "pclk2": 96000000, "tim1": 96000000, "tim2": 96000000, "ws": 3, "apb1_div": 2, "apb2_div": 1, "pll_n": 96, "pll_p": 2, "pll_q": 4,

### 2026-11-18 20:50:16 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk set 120"`
- Result: FAIL (exit code 2)
- COM10: status=timeout final=none (timeout) lines=2
- @R {"t": "clk", "plan": {"mhz": 120, "n": 120, "p": 2, "q": 5, "ws": 4, "apb1_div": 4, "apb2_div": 2, "usb_ok": true, "in_spec": false}}

### 2026-11-18 20:50:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=1

### 2026-11-18 20:50:29 — Run suite bench.cpu (start) [auto]

- Command: `run bench.cpu`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=d8384c2+dirty build=1795024148 busy=idle

### 2026-11-18 20:52:29 — hwtest.py run [auto]

- Command: `hwtest.py run bench.cpu`
- Result: FAIL (exit code 2)
- Suite bench.cpu: TIMEOUT results=32 pass=0 fail=0 (no output for 120.0s)
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-205023-bench.cpu.json

### 2026-11-18 20:52:33 — Board lock busy [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: ERROR locked by pid 18804 ('hwtest.py cmd "clk set 96"' since 2026-11-18T20:52:30+03:00)

### 2026-11-18 20:52:33 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 18804 ('hwtest.py cmd "clk set 96"' since 2026-11-18T20:52:30+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 20:52:40 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk set 96"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK abort lines=2

### 2026-11-18 20:52:40 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2

### 2026-11-18 20:52:52 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2

### 2026-11-18 20:53:14 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2

### 2026-11-18 20:53:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2

### 2026-11-18 20:53:38 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2

### 2026-11-18 20:53:42 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: FAIL (exit code 2)

### 2026-11-18 20:53:48 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: FAIL (exit code 2)

### 2026-11-18 20:53:59 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK info lines=2

### 2026-11-18 20:54:05 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd reset`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 20:54:09 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2
- @R {"t": "clk", "sysclk": 96000000, "hclk": 96000000, "pclk1": 48000000, "pclk2": 96000000, "tim1": 96000000, "tim2": 96000000, "ws": 3, "apb1_div": 2, "apb2_div": 1, "pll_n": 96, "pll_p": 2, "pll_q": 4,

### 2026-11-18 20:54:10 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2
- @R {"t": "clk", "sysclk": 96000000, "hclk": 96000000, "pclk1": 48000000, "pclk2": 96000000, "tim1": 96000000, "tim2": 96000000, "ws": 3, "apb1_div": 2, "apb2_div": 1, "pll_n": 96, "pll_p": 2, "pll_q": 4,

### 2026-11-18 20:54:44 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk set 120"`
- Result: FAIL (exit code 2)
- COM10: status=timeout final=none (timeout) lines=2
- @R {"t": "clk", "plan": {"mhz": 120, "n": 120, "p": 2, "q": 5, "ws": 4, "apb1_div": 4, "apb2_div": 2, "usb_ok": true, "in_spec": false}}

### 2026-11-18 20:54:45 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=1

### 2026-11-18 20:54:45 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=1

### 2026-11-18 20:54:46 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=1

### 2026-11-18 20:55:21 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd info`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2
- @R {"t": "info", "uid": "002800343435511331323538", "serial": "315A356C3435", "fw": "d8384c2+dirty", "build": 1795024148, "clk": 120000000, "opt": "-O2", "busy": "idle", "hwcal": true, "reset": "sft", "f

### 2026-11-18 20:55:32 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK info lines=2
- @R {"t": "clk", "sysclk": 120000000, "hclk": 120000000, "pclk1": 30000000, "pclk2": 60000000, "tim1": 60000000, "tim2": 120000000, "ws": 4, "apb1_div": 4, "apb2_div": 2, "pll_n": 120, "pll_p": 2, "pll_q"

### 2026-11-18 20:56:12 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.65 s
- cmd: port vanished right after 'dfu' (assumed success): serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 20:56:24 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.180
- Output: Time elapsed during verifying operation: 00:00:01.757
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 20:56:25 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (252456 bytes), expected id: {'fw': 'd8384c2+dirty', 'build': 1795024565}
- Programmer: exit code 0, verified=True
- App after start: running fw=d8384c2+dirty build=1795024565, CDC enumerations=1
- Timing: info_before=3.06s, app_to_dfu=0.65s, program=12.39s, dfu_to_app=0.71s, total=16.84s

### 2026-11-18 20:56:52 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk set 120"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 20:56:57 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2
- @R {"t": "clk", "sysclk": 120000000, "hclk": 120000000, "pclk1": 30000000, "pclk2": 60000000, "tim1": 60000000, "tim2": 120000000, "ws": 4, "apb1_div": 4, "apb2_div": 2, "pll_n": 120, "pll_p": 2, "pll_q"

### 2026-11-18 20:56:58 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK clk lines=2
- @R {"t": "clk", "sysclk": 120000000, "hclk": 120000000, "pclk1": 30000000, "pclk2": 60000000, "tim1": 60000000, "tim2": 120000000, "ws": 4, "apb1_div": 4, "apb2_div": 2, "pll_n": 120, "pll_p": 2, "pll_q"

### 2026-11-18 20:57:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "clk set 96"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 20:57:40 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 20:59:06 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 2, "slots": 2048, "seq": 2, "pending": 0, "boot_fail": ""}

### 2026-11-18 20:59:12 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=4
- @R {"t": "oc", "i": 0, "seq": 1, "ev": "result", "mhz": 96, "ws": 3, "apb1": 2, "apb2": 1, "step": 254, "pass": false, "reason": "audio", "checks": "1F", "cpu_mops": 24576, "lcd_fps10": 1440, "aud_ppm":
- @R {"t": "oc", "i": 1, "seq": 2, "ev": "note", "mhz": 0, "ws": 0, "apb1": 0, "apb2": 0, "step": 0, "pass": false, "reason": "iwdg", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_dC":
- @R {"t": "oc", "ev": "log_end", "records": 2, "slots": 2048, "boot_fail": ""}

### 2026-11-18 20:59:13 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=d8384c2+dirty build=1795024565 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 20:59:51 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=d8384c2+dirty build=1795024565 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:00:06 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=d8384c2+dirty build=1795024565 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:00:22 — Board lock busy [auto]

- Command: `hwtest.py info`
- Result: ERROR locked by pid 14296 ('hwtest.py flash --wait-lock 60 build/hwtest.bin' since 2026-11-18T21:00:22+03:00)

### 2026-11-18 21:00:22 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 14296 ('hwtest.py flash --wait-lock 60 build/hwtest.bin' since 2026-11-18T21:00:22+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 21:00:23 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-18 21:00:35 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.173
- Output: Time elapsed during verifying operation: 00:00:01.756
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:00:36 — hwtest.py flash [auto]

- Command: `hwtest.py flash --wait-lock 60 build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (252672 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795024809}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795024809, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=12.38s, dfu_to_app=0.70s, total=13.77s

### 2026-11-18 21:00:38 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:00:48 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=2
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:00:54 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:01:10 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:01:11 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:01:25 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=64000000 busy=oc.sweep reset=sft uid=002800343435511331323538

### 2026-11-18 21:01:41 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=oc.sweep reset=sft uid=002800343435511331323538

### 2026-11-18 21:01:52 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:01:57 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:02:13 — Board lock busy [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: ERROR locked by pid 20508 ('hwtest.py info' since 2026-11-18T21:02:13+03:00)

### 2026-11-18 21:02:13 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 20508 ('hwtest.py info' since 2026-11-18T21:02:13+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 21:02:13 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:02:23 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-18 21:02:29 — Board lock busy [auto]

- Command: `hwtest.py info`
- Result: ERROR locked by pid 18272 ('hwtest.py cmd "oc status"' since 2026-11-18T21:02:28+03:00)

### 2026-11-18 21:02:29 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 18272 ('hwtest.py cmd "oc status"' since 2026-11-18T21:02:28+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 21:02:29 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 18, "pending": 0, "boot_fail": "oc attempt 168 MHz ws6 FAILED: fault"}

### 2026-11-18 21:02:42 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=17
- @R {"t": "oc", "i": 2, "seq": 4, "ev": "result", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": true, "reason": "none", "checks": "FF", "cpu_mops": 47, "lcd_fps10": 720, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 3, "seq": 5, "ev": "attempt", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 4, "seq": 6, "ev": "result", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": true, "reason": "none", "checks": "FF", "cpu_mops": 63, "lcd_fps10": 960, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 5, "seq": 7, "ev": "attempt", "mhz": 84, "ws": 2, "apb1": 2, "apb2": 1, "step": 3, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 6, "seq": 8, "ev": "result", "mhz": 84, "ws": 2, "apb1": 2, "apb2": 1, "step": 3, "pass": true, "reason": "none", "checks": "FF", "cpu_mops": 83, "lcd_fps10": 1260, "aud_ppm": -21, "t

### 2026-11-18 21:02:44 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:03:00 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:03:16 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:03:26 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- cmd: @OK dfu

### 2026-11-18 21:03:32 — Board lock busy [auto]

- Command: `hwtest.py info`
- Result: ERROR locked by pid 6760 ('hwtest.py flash build/hwtest.bin' since 2026-11-18T21:03:25+03:00)

### 2026-11-18 21:03:32 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 6760 ('hwtest.py flash build/hwtest.bin' since 2026-11-18T21:03:25+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 21:03:38 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.157
- Output: Time elapsed during verifying operation: 00:00:01.756
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:03:39 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (252672 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795024809}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795024809, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.58s, program=12.36s, dfu_to_app=0.72s, total=13.73s

### 2026-11-18 21:03:48 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795024809 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:03:50 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:04:02 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.178
- Output: Time elapsed during verifying operation: 00:00:01.758
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:04:03 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (252792 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025025}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025025, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=12.38s, dfu_to_app=0.71s, total=13.77s

### 2026-11-18 21:04:03 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:04:11 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=2
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:04:11 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep list=48,64,84,96,100,120,144,152,160,168,192,216"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:04:19 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=48000000 busy=oc.sweep reset=sft uid=002800343435511331323538

### 2026-11-18 21:04:35 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:04:50 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=11
- @R {"t": "oc", "i": 8, "seq": 10, "ev": "result", "mhz": 96, "ws": 3, "apb1": 2, "apb2": 1, "step": 4, "pass": true, "reason": "none", "checks": "FF", "cpu_mops": 95, "lcd_fps10": 1440, "aud_ppm": -21, "
- @R {"t": "oc", "i": 9, "seq": 11, "ev": "attempt", "mhz": 100, "ws": 3, "apb1": 2, "apb2": 1, "step": 5, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "tem
- @R {"t": "oc", "i": 10, "seq": 12, "ev": "result", "mhz": 100, "ws": 3, "apb1": 2, "apb2": 1, "step": 5, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 99, "lcd_fps10": 759, "aud_ppm": -21,
- @R {"t": "oc", "i": 11, "seq": 13, "ev": "attempt", "mhz": 120, "ws": 4, "apb1": 4, "apb2": 2, "step": 6, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "te
- @R {"t": "oc", "i": 12, "seq": 14, "ev": "result", "mhz": 120, "ws": 4, "apb1": 4, "apb2": 2, "step": 6, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 119, "lcd_fps10": 910, "aud_ppm": -21,

### 2026-11-18 21:04:51 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:05:07 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:05:09 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=11
- @R {"t": "oc", "i": 8, "seq": 10, "ev": "result", "mhz": 96, "ws": 3, "apb1": 2, "apb2": 1, "step": 4, "pass": true, "reason": "none", "checks": "FF", "cpu_mops": 95, "lcd_fps10": 1440, "aud_ppm": -21, "
- @R {"t": "oc", "i": 9, "seq": 11, "ev": "attempt", "mhz": 100, "ws": 3, "apb1": 2, "apb2": 1, "step": 5, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "tem
- @R {"t": "oc", "i": 10, "seq": 12, "ev": "result", "mhz": 100, "ws": 3, "apb1": 2, "apb2": 1, "step": 5, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 99, "lcd_fps10": 759, "aud_ppm": -21,
- @R {"t": "oc", "i": 11, "seq": 13, "ev": "attempt", "mhz": 120, "ws": 4, "apb1": 4, "apb2": 2, "step": 6, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "te
- @R {"t": "oc", "i": 12, "seq": 14, "ev": "result", "mhz": 120, "ws": 4, "apb1": 4, "apb2": 2, "step": 6, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 119, "lcd_fps10": 910, "aud_ppm": -21,

### 2026-11-18 21:05:22 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:05:38 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:05:50 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q read 0xFE0000 256"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-18 21:05:54 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:06:10 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025025 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:06:12 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.57 s
- cmd: @OK dfu

### 2026-11-18 21:06:25 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.187
- Output: Time elapsed during verifying operation: 00:00:01.757
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:06:25 — Board lock busy [auto]

- Command: `hwtest.py info`
- Result: ERROR locked by pid 19756 ('hwtest.py flash build/hwtest.bin' since 2026-11-18T21:06:12+03:00)

### 2026-11-18 21:06:25 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid 19756 ('hwtest.py flash build/hwtest.bin' since 2026-11-18T21:06:12+03:00); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 21:06:26 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253104 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025168}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025168, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.57s, program=12.39s, dfu_to_app=0.71s, total=13.77s

### 2026-11-18 21:06:26 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=19
- @R {"t": "oc", "i": 0, "ev": "bad", "hex": "434F00000100000020000100", "crc_calc": "87C61ECB", "crc_rec": "00000080"}
- @R {"t": "oc", "i": 1, "ev": "bad", "hex": "434F01000200000000000000", "crc_calc": "6303660F", "crc_rec": "00008000"}
- @R {"t": "oc", "i": 2, "ev": "bad", "hex": "434F02010400000030000101", "crc_calc": "C6A09F4A", "crc_rec": "02003240"}
- @R {"t": "oc", "i": 3, "ev": "bad", "hex": "434F00000400000000000000", "crc_calc": "9A4B0359", "crc_rec": "30852202"}
- @R {"t": "oc", "i": 4, "ev": "bad", "hex": "434F02000400000000000000", "crc_calc": "83E351A5", "crc_rec": "1CA29141"}

### 2026-11-18 21:06:41 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:06:57 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:07:03 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 18, "pending": 0, "boot_fail": ""}

### 2026-11-18 21:07:07 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=2
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:07:08 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=2
- @R {"t": "oc", "ev": "log_end", "records": 0, "slots": 2048, "boot_fail": ""}

### 2026-11-18 21:07:13 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:07:26 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep list=96,120 max=0 t=2"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:07:29 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=oc.sweep reset=sft uid=002800343435511331323538

### 2026-11-18 21:07:40 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=19
- @R {"t": "oc", "i": 0, "ev": "bad", "hex": "434F00000100000020000100", "crc_calc": "87C61ECB", "crc_rec": "00000080"}
- @R {"t": "oc", "i": 1, "ev": "bad", "hex": "434F01000200000000000000", "crc_calc": "6303660F", "crc_rec": "00008000"}
- @R {"t": "oc", "i": 2, "ev": "bad", "hex": "434F02010400000020000100", "crc_calc": "C1960AEC", "crc_rec": "00001200"}
- @R {"t": "oc", "i": 3, "ev": "bad", "hex": "434F00000400000000000000", "crc_calc": "9A4B0359", "crc_rec": "20000200"}
- @R {"t": "oc", "i": 4, "ev": "bad", "hex": "434F02000400000000000000", "crc_calc": "2848A7E0", "crc_rec": "04001000"}

### 2026-11-18 21:07:41 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 18, "pending": 0, "boot_fail": ""}

### 2026-11-18 21:07:44 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:07:58 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 18, "pending": 0, "boot_fail": ""}

### 2026-11-18 21:08:00 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:08:16 — Board lock busy [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: ERROR locked by pid None (None since None)

### 2026-11-18 21:08:16 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: ERROR (exit code 3)
- Error: LockBusy: board is locked by pid None (None since None); use --wait-lock [SECONDS] to wait. Lock file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\.board.lock

### 2026-11-18 21:08:16 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:08:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=2
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:08:17 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep list=96,120 max=0 t=2"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:08:32 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=3516508+dirty build=1795025168 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-18 21:08:58 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=19
- @R {"t": "oc", "i": 0, "ev": "bad", "hex": "434F00000100000020000100", "crc_calc": "87C61ECB", "crc_rec": "00000080"}
- @R {"t": "oc", "i": 1, "ev": "bad", "hex": "434F01000200000000000000", "crc_calc": "6303660F", "crc_rec": "00008000"}
- @R {"t": "oc", "i": 2, "ev": "bad", "hex": "434F02010400000020000100", "crc_calc": "C1960AEC", "crc_rec": "00000000"}
- @R {"t": "oc", "i": 3, "ev": "bad", "hex": "434F00000400000000000000", "crc_calc": "9A4B0359", "crc_rec": "00000000"}
- @R {"t": "oc", "i": 4, "ev": "bad", "hex": "434F02000400000000000000", "crc_calc": "2848A7E0", "crc_rec": "04000000"}

### 2026-11-18 21:09:30 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=2
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:09:30 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan 0xFE0000 0x10000 4096"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "FE0000", "len": 65536, "step": 4096, "granules": 16, "blank": 15, "used": 1, "nonff_bytes": 535, "first_nonff": "FE0000", "last_nonff": "FE021F", "ranges": [["FE0000", "FE1

### 2026-11-18 21:09:38 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q info"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR w25q usage: w25q id | read <addr_hex> <len> | crc <addr_hex> <len> | scan <addr_hex> <len> [step] lines=1

### 2026-11-18 21:09:44 — Run suite w25q.info (start) [auto]

- Command: `run w25q.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=3516508+dirty build=1795025168 busy=idle

### 2026-11-18 21:09:44 — hwtest.py run [auto]

- Command: `hwtest.py run w25q.info`
- Result: OK (exit code 0)
- Suite w25q.info: PASS results=3 pass=3 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-210944-w25q.info.json

### 2026-11-18 21:10:05 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- cmd: @OK dfu

### 2026-11-18 21:10:17 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.203
- Output: Time elapsed during verifying operation: 00:00:01.757
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:10:18 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253352 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025400}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025400, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.58s, program=12.39s, dfu_to_app=0.71s, total=13.76s

### 2026-11-18 21:10:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR oc clear failed lines=5
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 0, "sr1": "02"}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 1, "sr1": "02"}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 2, "sr1": "02"}
- @R {"t": "oc", "ev": "clear", "ok": false}

### 2026-11-18 21:10:32 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 18, "pending": 0, "boot_fail": ""}

### 2026-11-18 21:10:35 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-18 21:10:47 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.193
- Output: Time elapsed during verifying operation: 00:00:01.757
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:10:48 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253360 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025431}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025431, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=12.39s, dfu_to_app=0.72s, total=13.81s

### 2026-11-18 21:10:49 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR oc clear failed lines=5
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 0, "sr1": "02"}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 1, "sr1": "02"}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 2, "sr1": "02"}
- @R {"t": "oc", "ev": "clear", "ok": false}

### 2026-11-18 21:11:16 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:11:28 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.187
- Output: Time elapsed during verifying operation: 00:00:01.757
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:11:29 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253376 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025471}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025471, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=12.39s, dfu_to_app=0.71s, total=13.77s

### 2026-11-18 21:11:29 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR oc clear failed lines=5
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 0, "sr1": "02", "lock": 1}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 1, "sr1": "02", "lock": 1}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 2, "sr1": "02", "lock": 1}
- @R {"t": "oc", "ev": "clear", "ok": false}

### 2026-11-18 21:12:43 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.57 s
- cmd: @OK dfu

### 2026-11-18 21:12:56 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.179
- Output: Time elapsed during verifying operation: 00:00:01.757
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:12:56 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253552 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025552}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025552, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.57s, program=12.37s, dfu_to_app=0.72s, total=13.73s

### 2026-11-18 21:12:57 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR oc clear failed lines=5
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 0, "sr1": "02", "lock": 1}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 1, "sr1": "02", "lock": 1}
- @R {"t": "oc", "ev": "erase_retry", "addr": "FE0000", "try": 2, "sr1": "02", "lock": 1}
- @R {"t": "oc", "ev": "clear", "ok": false}

### 2026-11-18 21:13:44 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:13:56 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.166
- Output: Time elapsed during verifying operation: 00:00:01.758
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:13:57 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253648 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025619}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025619, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=12.37s, dfu_to_app=0.70s, total=13.74s

### 2026-11-18 21:13:58 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=3
- @R {"t": "oc", "ev": "block_erase", "st": 0, "us": 282115, "sr1": "00"}
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:14:30 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:14:42 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 12.4 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:10.189
- Output: Time elapsed during verifying operation: 00:00:01.764
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:14:43 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (253472 bytes), expected id: {'fw': '3516508+dirty', 'build': 1795025666}
- Programmer: exit code 0, verified=True
- App after start: running fw=3516508+dirty build=1795025666, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=12.40s, dfu_to_app=0.70s, total=13.77s

### 2026-11-18 21:14:44 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep list=96,120 max=0 t=2"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:15:14 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=5
- @R {"t": "oc", "i": 0, "seq": 1, "ev": "attempt", "mhz": 96, "ws": 3, "apb1": 2, "apb2": 1, "step": 1, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 1, "seq": 2, "ev": "result", "mhz": 96, "ws": 3, "apb1": 2, "apb2": 1, "step": 1, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 95, "lcd_fps10": 1440, "aud_ppm": -21, "t
- @R {"t": "oc", "i": 2, "seq": 3, "ev": "note", "mhz": 0, "ws": 0, "apb1": 0, "apb2": 0, "step": 0, "pass": false, "reason": "sweep_end", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp
- @R {"t": "oc", "ev": "log_end", "records": 3, "slots": 2048, "boot_fail": ""}

### 2026-11-18 21:15:36 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log clear"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log clear lines=3
- @R {"t": "oc", "ev": "block_erase", "st": 0, "us": 276302, "sr1": "00"}
- @R {"t": "oc", "ev": "clear", "ok": true}

### 2026-11-18 21:15:36 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.sweep list=48,64,84,96,100,120,144,152,160,168,192,216"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:15:49 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:15:51 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:16:00 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:16:05 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:16:10 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: none
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-18 21:16:11 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:16:22 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-18 21:16:32 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 20, "pending": 0, "boot_fail": ""}

### 2026-11-18 21:16:41 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 17, "slots": 2048, "seq": 20, "pending": 0, "boot_fail": ""}

### 2026-11-18 21:17:01 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=19
- @R {"t": "oc", "i": 0, "seq": 4, "ev": "attempt", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 1, "seq": 5, "ev": "result", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 47, "lcd_fps10": 720, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 2, "seq": 6, "ev": "attempt", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 3, "seq": 7, "ev": "result", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 63, "lcd_fps10": 960, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 4, "seq": 8, "ev": "attempt", "mhz": 84, "ws": 2, "apb1": 2, "apb2": 1, "step": 3, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_

### 2026-11-18 21:17:12 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.ws"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:17:42 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 23, "slots": 2048, "seq": 26, "pending": 0, "boot_fail": "oc attempt 96 MHz ws1 FAILED: iwdg"}

### 2026-11-18 21:17:57 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 23, "slots": 2048, "seq": 26, "pending": 0, "boot_fail": "oc attempt 96 MHz ws1 FAILED: iwdg"}

### 2026-11-18 21:19:03 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 23, "slots": 2048, "seq": 26, "pending": 0, "boot_fail": "oc attempt 96 MHz ws1 FAILED: iwdg"}

### 2026-11-18 21:19:47 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=25
- @R {"t": "oc", "i": 0, "seq": 4, "ev": "attempt", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 1, "seq": 5, "ev": "result", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 47, "lcd_fps10": 720, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 2, "seq": 6, "ev": "attempt", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 3, "seq": 7, "ev": "result", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 63, "lcd_fps10": 960, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 4, "seq": 8, "ev": "attempt", "mhz": 84, "ws": 2, "apb1": 2, "apb2": 1, "step": 3, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_

### 2026-11-18 21:19:56 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.ws list=120,144"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:20:24 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=33
- @R {"t": "oc", "i": 0, "seq": 4, "ev": "attempt", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 1, "seq": 5, "ev": "result", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 47, "lcd_fps10": 720, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 2, "seq": 6, "ev": "attempt", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 3, "seq": 7, "ev": "result", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 63, "lcd_fps10": 960, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 4, "seq": 8, "ev": "attempt", "mhz": 84, "ws": 2, "apb1": 2, "apb2": 1, "step": 3, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_

### 2026-11-18 21:20:33 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "run oc.ws list=144"`
- Result: ERROR (exit code 2)
- Error: DeviceDisconnected: serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22))

### 2026-11-18 21:20:41 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR oc busy lines=1

### 2026-11-18 21:20:49 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc log"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc log lines=41
- @R {"t": "oc", "i": 0, "seq": 4, "ev": "attempt", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 1, "seq": 5, "ev": "result", "mhz": 48, "ws": 1, "apb1": 1, "apb2": 1, "step": 1, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 47, "lcd_fps10": 720, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 2, "seq": 6, "ev": "attempt", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_
- @R {"t": "oc", "i": 3, "seq": 7, "ev": "result", "mhz": 64, "ws": 2, "apb1": 2, "apb2": 1, "step": 2, "pass": true, "reason": "none", "checks": "7F", "cpu_mops": 63, "lcd_fps10": 960, "aud_ppm": -21, "te
- @R {"t": "oc", "i": 4, "seq": 8, "ev": "attempt", "mhz": 84, "ws": 2, "apb1": 2, "apb2": 1, "step": 3, "pass": false, "reason": "none", "checks": "00", "cpu_mops": 0, "lcd_fps10": 0, "aud_ppm": 0, "temp_

### Manual note (block B, agent)
- oc.sweep (default list + 152/160/168): 48..144 MHz pass; 152 MHz fails (CPU kernel checksum mismatch, no fault); 168 MHz HardFault (PC 0x28000010). Max stable 144 MHz.
- oc.ws (caches off): min flash WS 2 @96, 2 @120, 3 @144 (IWDG reset one step below).
- W25Q sector erase 0x20 at 0xFE0000 is rejected (WEL stays set); the OC log is cleared with a 64 KB block erase (0xD8) instead.
- USB kept alive across a PLL restart delivers stale/partial CDC data; clock_set_mhz now always stops USB cleanly and restarts it on a 48 MHz-capable clock.

### 2026-11-18 21:21:28 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 39, "slots": 2048, "seq": 42, "pending": 0, "boot_fail": "oc attempt 144 MHz ws2 FAILED: iwdg"}

### 2026-11-18 21:22:35 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "oc status"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK oc status lines=2
- @R {"t": "oc", "ev": "status", "slots_used": 39, "slots": 2048, "seq": 42, "pending": 0, "boot_fail": "oc attempt 144 MHz ws2 FAILED: iwdg"}

### 2026-11-18 21:41:00 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:41:15 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 14.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.587
- Output: Time elapsed during verifying operation: 00:00:01.899
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:41:15 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273824 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795027257}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795027257, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.93s, dfu_to_app=0.72s, total=16.32s

### 2026-11-18 21:41:29 — Run suite stress.game (start) [auto]

- Command: `run stress.game s=60`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027257 busy=idle

### 2026-11-18 21:42:29 — hwtest.py run [auto]

- Command: `hwtest.py run stress.game s=60`
- Result: FAIL (exit code 1)
- Suite stress.game: FAIL results=7 pass=0 fail=1
- Failed: stress.game/summary
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214129-stress.game.json

### 2026-11-18 21:42:52 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:43:07 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 14.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.605
- Output: Time elapsed during verifying operation: 00:00:01.897
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:43:08 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273832 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795027363}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795027363, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.95s, dfu_to_app=0.72s, total=16.33s

### 2026-11-18 21:43:08 — Run suite stress.game (start) [auto]

- Command: `run stress.game s=60`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:44:08 — hwtest.py run [auto]

- Command: `hwtest.py run stress.game s=60`
- Result: OK (exit code 0)
- Suite stress.game: PASS results=7 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214308-stress.game.json

### 2026-11-18 21:44:15 — Run suite soak (start) [auto]

- Command: `run soak min=3`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:47:15 — hwtest.py run [auto]

- Command: `hwtest.py run soak min=3`
- Result: OK (exit code 0)
- Suite soak: PASS results=19 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214415-soak.json

### 2026-11-18 21:47:16 — Run suite stress.game (start) [auto]

- Command: `run stress.game s=15 fps=0`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:47:31 — hwtest.py run [auto]

- Command: `hwtest.py run stress.game s=15 fps=0`
- Result: OK (exit code 0)
- Suite stress.game: PASS results=3 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214716-stress.game.json

### 2026-11-18 21:48:34 — Run suite stream.video (start) [auto]

- Command: `run stream.video`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:48:36 — hwtest.py run [auto]

- Command: `hwtest.py run stream.video --timeout 200`
- Result: FAIL (exit code 1)
- Suite stream.video: FAIL results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214834-stream.video.json

### 2026-11-18 21:48:37 — Run suite display.colmod (start) [auto]

- Command: `run display.colmod auto=1 t=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:48:43 — hwtest.py run [auto]

- Command: `hwtest.py run display.colmod auto=1 t=2`
- Result: OK (exit code 0)
- Suite display.colmod: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214837-display.colmod.json

### 2026-11-18 21:48:44 — Run suite power.ampdip (start) [auto]

- Command: `run power.ampdip`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:48:45 — hwtest.py run [auto]

- Command: `hwtest.py run power.ampdip`
- Result: OK (exit code 0)
- Suite power.ampdip: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214844-power.ampdip.json

### 2026-11-18 21:49:03 — Run suite stream.video (start) [auto]

- Command: `run stream.video`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027363 busy=idle

### 2026-11-18 21:49:04 — hwtest.py run [auto]

- Command: `hwtest.py run stream.video --timeout 200`
- Result: FAIL (exit code 1)
- Suite stream.video: FAIL results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-214903-stream.video.json

### 2026-11-18 21:49:05 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q read F80000 64"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-18 21:49:40 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q erase4k F82000"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q erase4k lines=2
- @R {"t": "w25q_erase4k", "addr": "F82000", "st": 0, "st_s": "ok", "sr1_before": "00", "sr1": "00", "us": 54981, "first16_blank": true}

### 2026-11-18 21:49:41 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q erase4k FDB000"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q erase4k lines=2
- @R {"t": "w25q_erase4k", "addr": "FDB000", "st": 0, "st_s": "ok", "sr1_before": "00", "sr1": "00", "us": 54780, "first16_blank": true}

### 2026-11-18 21:50:19 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q read F80000 48"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q read lines=2

### 2026-11-18 21:50:20 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "w25q scan F80000 0x5B000 4096"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK w25q scan lines=2
- @R {"t": "w25q_scan", "addr": "F80000", "len": 372736, "step": 4096, "granules": 91, "blank": 1, "used": 90, "nonff_bytes": 357649, "first_nonff": "F80000", "last_nonff": "FDAFFF", "ranges": [["F80000",

### 2026-11-18 21:50:48 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-18 21:51:03 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.628
- Output: Time elapsed during verifying operation: 00:00:01.897
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:51:03 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (274008 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795027844}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795027844, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=14.96s, dfu_to_app=0.71s, total=16.36s

### 2026-11-18 21:51:04 — Run suite stream.video (start) [auto]

- Command: `run stream.video`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027844 busy=idle

### 2026-11-18 21:51:05 — hwtest.py run [auto]

- Command: `hwtest.py run stream.video --timeout 200`
- Result: FAIL (exit code 1)
- Suite stream.video: FAIL results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215104-stream.video.json

### 2026-11-18 21:51:45 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.61 s
- cmd: @OK dfu

### 2026-11-18 21:52:00 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.618
- Output: Time elapsed during verifying operation: 00:00:01.903
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:52:01 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (274024 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795027902}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795027902, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.61s, program=14.96s, dfu_to_app=0.71s, total=16.36s

### 2026-11-18 21:52:02 — Run suite stream.video (start) [auto]

- Command: `run stream.video`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027902 busy=idle

### 2026-11-18 21:52:03 — hwtest.py run [auto]

- Command: `hwtest.py run stream.video --timeout 200`
- Result: FAIL (exit code 1)
- Suite stream.video: FAIL results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215202-stream.video.json

### 2026-11-18 21:52:38 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-18 21:52:53 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.616
- Output: Time elapsed during verifying operation: 00:00:01.906
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:52:54 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (274112 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795027955}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795027955, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=14.97s, dfu_to_app=0.72s, total=16.35s

### 2026-11-18 21:52:55 — Run suite stream.video (start) [auto]

- Command: `run stream.video`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795027955 busy=idle

### 2026-11-18 21:53:17 — hwtest.py run [auto]

- Command: `hwtest.py run stream.video --timeout 200`
- Result: OK (exit code 0)
- Suite stream.video: PASS results=8 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215255-stream.video.json

### 2026-11-18 21:53:33 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:53:48 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 14.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.540
- Output: Time elapsed during verifying operation: 00:00:01.896
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:53:49 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273864 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795028009}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795028009, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.89s, dfu_to_app=0.71s, total=16.27s

### 2026-11-18 21:53:50 — Run suite stream.video (start) [auto]

- Command: `run stream.video s=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028009 busy=idle

### 2026-11-18 21:53:58 — hwtest.py run [auto]

- Command: `hwtest.py run stream.video s=2`
- Result: OK (exit code 0)
- Suite stream.video: PASS results=5 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215349-stream.video.json

### 2026-11-18 21:53:59 — Run suite stress.game (start) [auto]

- Command: `run stress.game s=10`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028009 busy=idle

### 2026-11-18 21:54:09 — hwtest.py run [auto]

- Command: `hwtest.py run stress.game s=10`
- Result: OK (exit code 0)
- Suite stress.game: PASS results=2 pass=1 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215359-stress.game.json

### 2026-11-18 21:54:25 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:54:40 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.631
- Output: Time elapsed during verifying operation: 00:00:01.897
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:54:40 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273976 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795028061}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795028061, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.97s, dfu_to_app=0.71s, total=16.35s

### 2026-11-18 21:54:41 — Run suite power.ampdip (start) [auto]

- Command: `run power.ampdip`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028061 busy=idle

### 2026-11-18 21:54:42 — hwtest.py run [auto]

- Command: `hwtest.py run power.ampdip`
- Result: OK (exit code 0)
- Suite power.ampdip: PASS results=4 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215441-power.ampdip.json

### 2026-11-18 21:55:20 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:55:35 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.607
- Output: Time elapsed during verifying operation: 00:00:01.899
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:55:36 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273864 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795028116}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795028116, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.95s, dfu_to_app=0.71s, total=16.33s

### 2026-11-18 21:55:37 — Run suite power.ampdip (start) [auto]

- Command: `run power.ampdip`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028116 busy=idle

### 2026-11-18 21:55:37 — hwtest.py run [auto]

- Command: `hwtest.py run power.ampdip`
- Result: OK (exit code 0)
- Suite power.ampdip: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215537-power.ampdip.json

### 2026-11-18 21:55:50 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:56:05 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 14.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.593
- Output: Time elapsed during verifying operation: 00:00:01.912
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:56:05 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273864 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795028146}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795028146, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.94s, dfu_to_app=0.71s, total=16.32s

### 2026-11-18 21:56:06 — Run suite power.ampdip (start) [auto]

- Command: `run power.ampdip`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028146 busy=idle

### 2026-11-18 21:56:07 — hwtest.py run [auto]

- Command: `hwtest.py run power.ampdip`
- Result: OK (exit code 0)
- Suite power.ampdip: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215606-power.ampdip.json

### 2026-11-18 21:56:08 — Run suite power.sleep (start) [auto]

- Command: `run power.sleep`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028146 busy=idle

### 2026-11-18 21:56:08 — hwtest.py run [auto]

- Command: `hwtest.py run power.sleep --timeout 40`
- Result: FAIL (exit code 2)
- Suite power.sleep: DISCONNECTED results=7 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215608-power.sleep.json

### 2026-11-18 21:56:22 — Run suite power.sleep (start) [auto]

- Command: `run power.sleep report`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028146 busy=idle

### 2026-11-18 21:56:22 — hwtest.py run [auto]

- Command: `hwtest.py run power.sleep report`
- Result: OK (exit code 0)
- Suite power.sleep: PASS results=4 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215622-power.sleep.json

### 2026-11-18 21:56:35 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.59 s
- cmd: @OK dfu

### 2026-11-18 21:56:50 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 14.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.591
- Output: Time elapsed during verifying operation: 00:00:01.897
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 21:56:51 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273912 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795028192}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795028192, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.59s, program=14.92s, dfu_to_app=0.72s, total=16.30s

### 2026-11-18 21:56:52 — Run suite power.sleep (start) [auto]

- Command: `run power.sleep`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028192 busy=idle

### 2026-11-18 21:56:52 — hwtest.py run [auto]

- Command: `hwtest.py run power.sleep --timeout 40`
- Result: FAIL (exit code 2)
- Suite power.sleep: DISCONNECTED results=8 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215652-power.sleep.json

### 2026-11-18 21:57:01 — Run suite power.sleep (start) [auto]

- Command: `run power.sleep report`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028192 busy=idle

### 2026-11-18 21:57:01 — hwtest.py run [auto]

- Command: `hwtest.py run power.sleep report`
- Result: OK (exit code 0)
- Suite power.sleep: PASS results=4 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215701-power.sleep.json

### 2026-11-18 21:57:10 — Run suite input.latency (start) [auto]

- Command: `run input.latency t=5`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028192 busy=idle

### 2026-11-18 21:57:15 — hwtest.py run [auto]

- Command: `hwtest.py run input.latency t=5`
- Result: FAIL (exit code 2)
- Suite input.latency: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261118-215710-input.latency.json

### Manual note (block C, agent)
- stress.game s=60 (96 MHz, VBUS present): 3601 frames @60 fps, 0 missed, frame max 7.2 ms (avg 5.1 ms, CPU ~31 %), audio 2585/2585 blocks, 0 late/underrun, 0 TE/DME, 0 CRC errors/3601 chunks; unlimited fps = 138.8 fps, still clean. soak min=3 clean (temp 29.4 C, Vbat 4.18 V). FEIF is set ~19x/frame on the LCD stream DMA2 S3 (direct mode, benign, reported as fe_lcd, not a failure). PL: audio S5 3, LCD S3 2, W25Q RX 2 / TX 1, ADC S4 1.
- w25q_erase_sector() (Src/drv/w25q.c) returned OK in ~11 us without erasing in the stream.video loop (data then AND-ed onto old content); w25q_erase_cmd(0x20, ...) (55-65 ms, waits on BUSY) works. Block-C suites use erase_cmd; worth fixing/understanding the driver function.
- power.ampdip on USB (VBUS present): amp enable / 1 kHz -12 dBFS tone / amp off give <= ~14 mV dip against +-6 mV noise at 24 kS/s; ADC channel reads ~0.1 V for the first ~45 ms after ADC start (start-up artefact, pre-roll 120 ms used). Needs a battery-only repeat for F6.
- power.sleep: Stop wake clock restart ~14.5k cyc at HSI = ~0.9 ms, LCD wake 132 ms, USB re-enumeration ~350 ms after usb_stop/start (stale CONFIGURED after plain DevConnect), WFI wake 28 cyc median (0.29 us).

### 2026-11-18 22:00:32 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.58 s
- cmd: @OK dfu

### 2026-11-18 22:00:47 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 14.9 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.609
- Output: Time elapsed during verifying operation: 00:00:01.897
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-18 22:00:48 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273656 bytes), expected id: {'fw': '9c91e4f+dirty', 'build': 1795028428}
- Programmer: exit code 0, verified=True
- App after start: running fw=9c91e4f+dirty build=1795028428, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.58s, program=14.94s, dfu_to_app=0.71s, total=16.31s

### 2026-11-18 22:01:11 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save" --timeout 15`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save lines=2
- @R {"t": "hwcal_save", "seq": 4, "slot": "B"}

### 2026-11-18 22:01:13 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save" --timeout 15`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save lines=2
- @R {"t": "hwcal_save", "seq": 5, "slot": "A"}

### 2026-11-18 22:01:14 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save" --timeout 15`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save lines=2
- @R {"t": "hwcal_save", "seq": 6, "slot": "B"}

### 2026-11-18 22:01:45 — W25Q erase fix (manual note)

- Block C agent found `w25q_erase_sector()` returning OK in ~11 us without erasing (likely the F4 root symptom). It now uses `w25q_erase_cmd(0x20)` and retries if the erase completes in < 1 ms. Verified: `hwcal save` x3 (seq 4 B, 5 A after a sector erase, 6 B) all OK.

### 2026-11-19 10:30:53 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: FAIL (exit code 2)

### 2026-11-19 10:30:54 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd gauge --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK gauge lines=2
- @R {"t": "gauge", "mode": "NO_BATT", "level": 0, "valid": true, "flags": 4, "soc": 0.0, "vcomp_mv": 2087, "vbat_mv": 4199, "load_ma": 38, "load_state": 7, "vbus": true, "chg": false, "drops": 0, "rises":

### 2026-11-19 10:31:01 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: FAIL (exit code 2)

### 2026-11-19 10:31:10 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: FAIL (exit code 2)

### 2026-11-19 10:31:23 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd abort --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK abort lines=1

### 2026-11-19 10:31:24 — Run suite power.info (start) [auto]

- Command: `run power.info`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028428 busy=idle

### 2026-11-19 10:31:26 — hwtest.py run [auto]

- Command: `hwtest.py run power.info`
- Result: FAIL (exit code 1)
- Suite power.info: FAIL results=1 pass=0 fail=1
- Failed: power.info/info
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-103124-power.info.json

### 2026-11-19 10:31:37 — Run suite stress.game (start) [auto]

- Command: `run stress.game s=20 fps=0`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028428 busy=idle

### 2026-11-19 10:31:57 — hwtest.py run [auto]

- Command: `hwtest.py run stress.game s=20 fps=0 --timeout 90`
- Result: FAIL (exit code 1)
- Suite stress.game: FAIL results=3 pass=0 fail=1
- Failed: stress.game/summary
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-103137-stress.game.json

### 2026-11-19 10:54:42 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd abort --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK abort lines=1

### 2026-11-19 11:11:07 — Audio session with phone (manual note)

- phyphox Audio Amplitude (iPhone), speaker connected, battery connected, fan noise ~-55 dB. audio.sweep -> fr.csv/fr.png; audio.chirp -> chirp_fr.csv/fr_combined.png; audio.hiss (2 cm) -> hiss masked by fan; audio.clip -> saturation at ~-8 dBFS, 200 Hz rattle; speaker got very hot at the end (0 dBFS 200 Hz). Files in tools/results/.../audio-20261119-sweep/.
- No-battery check (earlier today): presence no_batt, Vbat reading floats 0..4.2 V, stress.game on USB only at 139 fps with 0 audio underruns / 0 DMA / 0 CRC errors (fail only from adc_bad).

### 2026-11-19 11:13:28 — Run suite audio.pop (start) [auto]

- Command: `run audio.pop t=300`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=9c91e4f+dirty build=1795028428 busy=idle

### 2026-11-19 11:18:02 — hwtest.py run [auto]

- Command: `hwtest.py run audio.pop t=300 --prompt-timeout 600`
- Result: OK (exit code 0)
- Suite audio.pop: PASS results=54 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-111328-audio.pop.json

### 2026-11-19 11:18:40 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.95 s
- cmd: @OK dfu

### 2026-11-19 11:18:55 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.1 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.684
- Output: Time elapsed during verifying operation: 00:00:01.903
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-19 11:18:56 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273696 bytes), expected id: {'fw': '581fd99+dirty', 'build': 1795076310}
- Programmer: exit code 0, verified=True
- App after start: running fw=581fd99+dirty build=1795076310, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.95s, program=15.15s, dfu_to_app=0.71s, total=16.88s

### 2026-11-19 11:19:01 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal get" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal get lines=2
- @R {"t": "hwcal", "status": "valid", "seq": 6, "slot": "B", "version": 1, "madctl_land": 160, "xoff_land": 1, "yoff_land": 2, "madctl_port": 0, "xoff_port": 2, "yoff_port": 1, "bgr": 0, "invert": 0, "gam

### 2026-11-19 11:19:08 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set amp_ramp_ms=0" --timeout 10`
- Result: FAIL (exit code 1)
- COM10: status=err final=@ERR hwcal unknown field lines=1

### 2026-11-19 11:19:27 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-19 11:19:42 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.0 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.660
- Output: Time elapsed during verifying operation: 00:00:01.898
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-19 11:19:43 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (273712 bytes), expected id: {'fw': '581fd99+dirty', 'build': 1795076363}
- Programmer: exit code 0, verified=True
- App after start: running fw=581fd99+dirty build=1795076363, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=15.02s, dfu_to_app=0.71s, total=16.40s

### 2026-11-19 11:20:18 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal set amp_ramp_ms=0" --timeout 10`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal set lines=1

### 2026-11-19 11:20:20 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "hwcal save" --timeout 15`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK hwcal save lines=2
- @R {"t": "hwcal_save", "seq": 7, "slot": "A"}

### 2026-11-19 11:20:45 — audio.pop by ear (manual note)

- User: 'naive' (amp enabled after PWM at mid) has no pop; all ramped variants (PWM ramp with amp on) pop. Idle hiss audible while the amp is on, stops when it turns off. Pop-free sequence made the default (audio_amp_on precharge 30 ms), HWCAL amp_ramp_ms=0 saved (seq 7).

### 2026-11-19 11:39:57 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.63 s
- cmd: @OK dfu

### 2026-11-19 11:40:12 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.785
- Output: Time elapsed during verifying operation: 00:00:01.953
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-19 11:40:13 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (281836 bytes), expected id: {'fw': '4d8ac13+dirty', 'build': 1795077587}
- Programmer: exit code 0, verified=True
- App after start: running fw=4d8ac13+dirty build=1795077587, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.63s, program=15.22s, dfu_to_app=0.71s, total=16.69s

### 2026-11-19 11:42:51 — hwtest.py media [auto]

- Command: `hwtest.py media upload tools/build/bbb.bin`
- Result: OK (exit code 0)
- media upload 348160/13239496 (3%) 0.34 MB/s
- media upload 688128/13239496 (5%) 0.34 MB/s
- media upload 1028096/13239496 (8%) 0.34 MB/s
- media upload 1363968/13239496 (10%) 0.34 MB/s
- media upload 1703936/13239496 (13%) 0.34 MB/s
- media upload 2043904/13239496 (15%) 0.34 MB/s
- media upload 2383872/13239496 (18%) 0.34 MB/s
- media upload 2723840/13239496 (21%) 0.34 MB/s
- media upload 3059712/13239496 (23%) 0.34 MB/s
- media upload 3399680/13239496 (26%) 0.34 MB/s
- media upload 3739648/13239496 (28%) 0.34 MB/s
- media upload 4079616/13239496 (31%) 0.34 MB/s
- media upload 4419584/13239496 (33%) 0.34 MB/s
- media upload 4759552/13239496 (36%) 0.34 MB/s
- media upload 5099520/13239496 (39%) 0.34 MB/s
- media upload 5439488/13239496 (41%) 0.34 MB/s
- media upload 5779456/13239496 (44%) 0.34 MB/s
- media upload 6119424/13239496 (46%) 0.34 MB/s
- media upload 6459392/13239496 (49%) 0.34 MB/s
- media upload 6799360/13239496 (51%) 0.34 MB/s
- media upload 7139328/13239496 (54%) 0.34 MB/s
- media upload 7479296/13239496 (56%) 0.34 MB/s
- media upload 7819264/13239496 (59%) 0.34 MB/s
- media upload 8159232/13239496 (62%) 0.34 MB/s
- media upload 8499200/13239496 (64%) 0.34 MB/s
- media upload 8839168/13239496 (67%) 0.34 MB/s
- media upload 9179136/13239496 (69%) 0.34 MB/s
- media upload 9519104/13239496 (72%) 0.34 MB/s
- media upload 9859072/13239496 (74%) 0.34 MB/s
- media upload 10199040/13239496 (77%) 0.34 MB/s
- media upload 10539008/13239496 (80%) 0.34 MB/s
- media upload 10878976/13239496 (82%) 0.34 MB/s
- media upload 11218944/13239496 (85%) 0.34 MB/s
- media upload 11558912/13239496 (87%) 0.34 MB/s
- media upload 11898880/13239496 (90%) 0.34 MB/s
- media upload 12238848/13239496 (92%) 0.34 MB/s
- media upload 12578816/13239496 (95%) 0.34 MB/s
- media upload 12918784/13239496 (98%) 0.34 MB/s
- media upload 13239496/13239496 (100%) 0.34 MB/s
- media upload: {"ok": true, "bytes": 13239496, "host_crc": 3945181159, "host_s": 39.33, "host_MBps": 0.337, "device": {"t": "media_write", "addr": "200000", "len": 13239496, "got": 13239496, "crc": 3945181159, "rx_crc": 3945181159, "ok": true, "err": "", "blocks_erased": 203, "rx_ms": 39388, "erase_ms": 2, "program_ms": 35402, "verify_ms": 6518, "kBps": 336}}

### 2026-11-19 11:43:10 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-19 11:43:25 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.3 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.844
- Output: Time elapsed during verifying operation: 00:00:01.954
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-19 11:43:26 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (281916 bytes), expected id: {'fw': '4d8ac13+dirty', 'build': 1795077786}
- Programmer: exit code 0, verified=True
- App after start: running fw=4d8ac13+dirty build=1795077786, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.62s, program=15.26s, dfu_to_app=0.70s, total=16.67s

### 2026-11-19 11:43:33 — hwtest.py cmd [auto]

- Command: `hwtest.py cmd "media info"`
- Result: OK (exit code 0)
- COM10: status=ok final=@OK media info lines=2
- @R {"t": "media_info", "valid": true, "why": "ok", "magic": "564D4253", "version": 1, "w": 160, "h": 128, "fps": 15, "frames": 600, "audio_rate": 16005, "audio_bits": 8, "frame_stride": 22059, "audio_byt

### 2026-11-19 11:43:34 — Run suite media.play (start) [auto]

- Command: `run media.play auto=10`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077786 busy=idle

### 2026-11-19 11:43:40 — hwtest.py run [auto]

- Command: `hwtest.py run media.play auto=10 --timeout 60`
- Result: OK (exit code 0)
- Suite media.play: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114334-media.play.json

### 2026-11-19 11:43:58 — Run suite media.play (start) [auto]

- Command: `run media.play auto=20`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077786 busy=idle

### 2026-11-19 11:44:01 — hwtest.py run [auto]

- Command: `hwtest.py run media.play auto=20 --timeout 60`
- Result: OK (exit code 0)
- Suite media.play: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114358-media.play.json

### 2026-11-19 11:44:28 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.62 s
- cmd: @OK dfu

### 2026-11-19 11:44:44 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-19 11:45:08 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.831
- Output: Time elapsed during verifying operation: 00:00:01.955
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-19 11:45:08 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (281924 bytes), expected id: {'fw': '4d8ac13+dirty', 'build': 1795077889}
- Programmer: exit code 0, verified=True
- App after start: running fw=4d8ac13+dirty build=1795077889, CDC enumerations=1
- Timing: program=15.22s, dfu_to_app=0.73s, total=16.11s

### 2026-11-19 11:46:57 — hwtest.py media [auto]

- Command: `hwtest.py media upload tools/build/bbb.bin`
- Result: OK (exit code 0)
- media upload 143360/13239496 (1%) 0.12 MB/s
- media upload 307200/13239496 (2%) 0.14 MB/s
- media upload 471040/13239496 (4%) 0.14 MB/s
- media upload 643072/13239496 (5%) 0.14 MB/s
- media upload 798720/13239496 (6%) 0.14 MB/s
- media upload 958464/13239496 (7%) 0.14 MB/s
- media upload 1126400/13239496 (9%) 0.14 MB/s
- media upload 1294336/13239496 (10%) 0.15 MB/s
- media upload 1454080/13239496 (11%) 0.14 MB/s
- media upload 1626112/13239496 (12%) 0.15 MB/s
- media upload 1781760/13239496 (13%) 0.14 MB/s
- media upload 1949696/13239496 (15%) 0.15 MB/s
- media upload 2109440/13239496 (16%) 0.14 MB/s
- media upload 2277376/13239496 (17%) 0.15 MB/s
- media upload 2437120/13239496 (18%) 0.14 MB/s
- media upload 2609152/13239496 (20%) 0.15 MB/s
- media upload 2764800/13239496 (21%) 0.14 MB/s
- media upload 2940928/13239496 (22%) 0.15 MB/s
- media upload 3092480/13239496 (23%) 0.14 MB/s
- media upload 3256320/13239496 (25%) 0.15 MB/s
- media upload 3420160/13239496 (26%) 0.14 MB/s
- media upload 3579904/13239496 (27%) 0.15 MB/s
- media upload 3747840/13239496 (28%) 0.14 MB/s
- media upload 3923968/13239496 (30%) 0.15 MB/s
- media upload 4075520/13239496 (31%) 0.15 MB/s
- media upload 4243456/13239496 (32%) 0.15 MB/s
- media upload 4403200/13239496 (33%) 0.15 MB/s
- media upload 4567040/13239496 (34%) 0.15 MB/s
- media upload 4730880/13239496 (36%) 0.15 MB/s
- media upload 4890624/13239496 (37%) 0.15 MB/s
- media upload 5058560/13239496 (38%) 0.15 MB/s
- media upload 5226496/13239496 (39%) 0.15 MB/s
- media upload 5386240/13239496 (41%) 0.15 MB/s
- media upload 5558272/13239496 (42%) 0.15 MB/s
- media upload 5713920/13239496 (43%) 0.15 MB/s
- media upload 5881856/13239496 (44%) 0.15 MB/s
- media upload 6041600/13239496 (46%) 0.15 MB/s
- media upload 6209536/13239496 (47%) 0.15 MB/s
- media upload 6361088/13239496 (48%) 0.15 MB/s
- media upload 6500352/13239496 (49%) 0.14 MB/s
- media upload 6643712/13239496 (50%) 0.14 MB/s
- media upload 6791168/13239496 (51%) 0.14 MB/s
- media upload 6942720/13239496 (52%) 0.15 MB/s
- media upload 7090176/13239496 (54%) 0.14 MB/s
- media upload 7241728/13239496 (55%) 0.14 MB/s
- media upload 7385088/13239496 (56%) 0.14 MB/s
- media upload 7528448/13239496 (57%) 0.14 MB/s
- media upload 7680000/13239496 (58%) 0.14 MB/s
- media upload 7827456/13239496 (59%) 0.14 MB/s
- media upload 7979008/13239496 (60%) 0.14 MB/s
- media upload 8126464/13239496 (61%) 0.14 MB/s
- media upload 8269824/13239496 (62%) 0.14 MB/s
- media upload 8417280/13239496 (64%) 0.14 MB/s
- media upload 8568832/13239496 (65%) 0.14 MB/s
- media upload 8724480/13239496 (66%) 0.14 MB/s
- media upload 8859648/13239496 (67%) 0.14 MB/s
- media upload 9007104/13239496 (68%) 0.14 MB/s
- media upload 9158656/13239496 (69%) 0.14 MB/s
- media upload 9297920/13239496 (70%) 0.14 MB/s
- media upload 9449472/13239496 (71%) 0.14 MB/s
- media upload 9588736/13239496 (72%) 0.14 MB/s
- media upload 9740288/13239496 (74%) 0.14 MB/s
- media upload 9887744/13239496 (75%) 0.14 MB/s
- media upload 10035200/13239496 (76%) 0.14 MB/s
- media upload 10170368/13239496 (77%) 0.14 MB/s
- media upload 10317824/13239496 (78%) 0.14 MB/s
- media upload 10465280/13239496 (79%) 0.14 MB/s
- media upload 10604544/13239496 (80%) 0.14 MB/s
- media upload 10752000/13239496 (81%) 0.14 MB/s
- media upload 10891264/13239496 (82%) 0.14 MB/s
- media upload 11046912/13239496 (83%) 0.14 MB/s
- media upload 11194368/13239496 (85%) 0.14 MB/s
- media upload 11345920/13239496 (86%) 0.14 MB/s
- media upload 11481088/13239496 (87%) 0.14 MB/s
- media upload 11620352/13239496 (88%) 0.14 MB/s
- media upload 11771904/13239496 (89%) 0.14 MB/s
- media upload 11919360/13239496 (90%) 0.14 MB/s
- media upload 12070912/13239496 (91%) 0.14 MB/s
- media upload 12226560/13239496 (92%) 0.14 MB/s
- media upload 12374016/13239496 (93%) 0.14 MB/s
- media upload 12517376/13239496 (95%) 0.14 MB/s
- media upload 12660736/13239496 (96%) 0.14 MB/s
- media upload 12812288/13239496 (97%) 0.14 MB/s
- media upload 12972032/13239496 (98%) 0.14 MB/s
- media upload 13115392/13239496 (99%) 0.14 MB/s
- media upload 13239496/13239496 (100%) 0.14 MB/s
- media upload: {"ok": true, "bytes": 13239496, "host_crc": 3945181159, "host_s": 93.72, "host_MBps": 0.141, "device": {"t": "media_write", "addr": "200000", "len": 13239496, "got": 13239496, "crc": 3945181159, "rx_crc": 3945181159, "ok": true, "err": "", "blocks_erased": 203, "rx_ms": 94070, "erase_ms": 54723, "program_ms": 35364, "verify_ms": 6518, "kBps": 140}}

### 2026-11-19 11:46:59 — Run suite demo (start) [auto]

- Command: `run demo auto=1 t=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077889 busy=idle

### 2026-11-19 11:47:05 — hwtest.py run [auto]

- Command: `hwtest.py run demo auto=1 t=2 --timeout 60`
- Result: OK (exit code 0)
- Suite demo: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114659-demo.json

### 2026-11-19 11:47:06 — Run suite demo (start) [auto]

- Command: `run demo auto=1 t=2 scene=0 col=2`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077889 busy=idle

### 2026-11-19 11:47:08 — hwtest.py run [auto]

- Command: `hwtest.py run demo auto=1 t=2 scene=0 col=2 --timeout 60`
- Result: OK (exit code 0)
- Suite demo: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114706-demo.json

### 2026-11-19 11:47:08 — Run suite demo (start) [auto]

- Command: `run demo auto=1 t=2 scene=0 col=3`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077889 busy=idle

### 2026-11-19 11:47:11 — hwtest.py run [auto]

- Command: `hwtest.py run demo auto=1 t=2 scene=0 col=3 --timeout 60`
- Result: OK (exit code 0)
- Suite demo: PASS results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114708-demo.json

### 2026-11-19 11:47:11 — Run suite display.colmod (start) [auto]

- Command: `run display.colmod auto=1 t=1`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077889 busy=idle

### 2026-11-19 11:47:15 — hwtest.py run [auto]

- Command: `hwtest.py run display.colmod auto=1 t=1 --timeout 60`
- Result: OK (exit code 0)
- Suite display.colmod: PASS results=3 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114711-display.colmod.json

### 2026-11-19 11:47:53 — Run suite input.latency (start) [auto]

- Command: `run input.latency`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795077889 busy=idle

### 2026-11-19 11:48:53 — hwtest.py run [auto]

- Command: `hwtest.py run input.latency --timeout 8`
- Result: FAIL (exit code 2)
- Suite input.latency: ABORT results=1 pass=0 fail=0
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-114753-input.latency.json

### 2026-11-19 11:48:54 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: OK (exit code 0)
- COM10: fw=4d8ac13+dirty build=1795077889 clk=96000000 busy=idle reset=sft uid=002800343435511331323538

### 2026-11-19 11:51:08 — Enter DFU from application (cmd) [auto]

- Command: `dfu command on COM10 (sn=315A356C3435)`
- Result: OK DFU device USB1 sn=315A356C3435 after 0.60 s
- cmd: @OK dfu

### 2026-11-19 11:51:23 — STM32_Programmer_CLI [auto]

- Command: `STM32_Programmer_CLI -c port=USB1 sn=315A356C3435 -w build\hwtest.bin 0x08000000 -v -s 0x08000000`
- Result: OK (exit code 0, 15.2 s)
- Output: DFU protocol: 1.1
- Output: Device ID : 0x431
- Output: Device name : STM32F411xC/E
- Output: Flash size : 512 KBytes (default)
- Output: File download complete
- Output: Time elapsed during download operation: 00:00:12.837
- Output: Time elapsed during verifying operation: 00:00:01.954
- Output: Download verified successfully
- Output: Start operation achieved successfully

### 2026-11-19 11:51:24 — hwtest.py flash [auto]

- Command: `hwtest.py flash build/hwtest.bin`
- Result: OK (exit code 0)
- Image: build\hwtest.bin (281924 bytes), expected id: {'fw': '4d8ac13+dirty', 'build': 1795078228}
- Programmer: exit code 0, verified=True
- App after start: running fw=4d8ac13+dirty build=1795078228, CDC enumerations=1
- Timing: info_before=0.05s, app_to_dfu=0.60s, program=15.24s, dfu_to_app=0.71s, total=16.62s

- 2026-11-19 (manual) media: W25Q 64K erase (0xD8) was silently skipped on the first upload (erase 2 ms total, F4 again); retry-on-<1ms fixed it (203 real erases = 55 s). Upload 13.2 MB ~94 s. media.play NOT run on hardware (user hold). Two earlier trial runs ended after 3-6 s with auto=10/20 and no key pressed (suspected spurious B edge); auto mode now ignores keys.

### 2026-11-19 12:03:28 — Run suite input.latency (start) [auto]

- Command: `run input.latency n=20 t=900`
- Result: started on COM10 sn=315A356C3435
- Firmware: fw=4d8ac13+dirty build=1795078228 busy=idle

### 2026-11-19 12:05:32 — hwtest.py run [auto]

- Command: `hwtest.py run input.latency n=20 t=900 --prompt-timeout 1800`
- Result: FAIL (exit code 2)
- Suite input.latency: DISCONNECTED results=0 pass=0 fail=0 (serial port lost: ClearCommError failed (PermissionError(13, 'The device does not recognize the command.', None, 22)))
- Results file: C:\Users\Bool\Projects\STM-boy\.claude\worktrees\hwtest-firmware\software\stm32-hwtest\tools\results\002800343435511331323538\20261119-120328-input.latency.json

### 2026-11-19 12:05:42 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-19 12:06:04 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: none
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-19 13:11:27 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: none
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-19 13:11:27 — hwtest.py info [auto]

- Command: `hwtest.py info`
- Result: ERROR (exit code 2)
- Error: NoDevice: no STM-boy CDC port (0483:5740) found

### 2026-11-19 13:25:50 — Optical latency attempt (manual note)

- OWON XDS3202A: CH1 = PA8 marker (R24), CH2 = yellow LED as light sensor (x1, 10 mV/div, 20 MHz BW). Marker OK, LED gave no visible step (earlier 80 Hz ripple at x10 was pickup). No photodiode available -> optical latency skipped; documented a derived estimate of ~20-40 ms button-to-pixel.

### 2026-11-19 13:38:36 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-19 13:42:55 — hwtest.py ports [auto]

- Command: `hwtest.py ports`
- Result: OK (exit code 0)
- CDC: COM10 sn=315A356C3435
- DFU (CLI): none
- DFU (PnP): none

### 2026-11-19 13:45:55 — Currents: off state and S12 (manual note)

- Board OFF (ATtiny only, 3V3 disabled): ~0.004 mA = ~4 uA on the meter's 2 mA range -> ~35 mAh/year, negligible vs cell self-discharge.
- S12 (BL100 + LCD DMA full frames + CPU burn) on battery: 60.5 mA (200 mA range, USB unplugged right after start). ~239 mW at ~3.95 V.
- Re-ran battlog_analyze.py with --p12 239: delivered ~500 mAh / 1.88 Wh to the 3.30 V cutoff - 60 %% above the 310 mAh nominal of LP233350. Unresolved: verify the cell marking and get an independent capacity from a full logged charge (BQ24075 CC ~175 mA).
- Bug: S12 load progress bar on the left is drawn in landscape order under native mode (scrambled).

### 2026-11-19 13:55:44 — Current vs SYSCLK (manual note)

- demo raytracer (compute-bound), BL 100 %, COLMOD 16-bit, on battery, meter 200 mA range: 48 MHz 50.9 mA 20.3 fps; 72: 57.3 / 30.4; 84: 60.6 / 35.9; 96: 63.3 / 40.7; 120: 66.8 / 51.0; 144: 69.3 / 61.0.
- Linear fit: 0.191 mA/MHz + 43.5 mA (static part dominated by the backlight). fps scales exactly with SYSCLK. Charge per frame falls with frequency (2.51 -> 1.14 mA*s/frame): for compute-bound work higher clock is MORE energy-efficient (fixed backlight/system power amortised).

### 2026-11-19 15:31:29 — Scope: amp-enable dip on battery (manual note)

- OWON XDS3202A, battery only (no meter, no USB), CH1 AC 50 mV/div 20 MHz BW on C14 (+V) and in a second run on C15 (VBAT), CH2 DC on R23 (AMP_SD) as trigger (rising, 1.5 V), 20 ms/div and 10 us/div. power.ampdip started from the device menu.
- Result: no visible step/dip at amp enable or during the 1 kHz tone on either rail (< ~15 mV vs +-40 mV noise band) -> F6 closed (was the meter burden). Periodic ~50-65 mV negative pulses every ~17 us on both rails before and after amp enable = TPS63802 power-save bursts -> explains F10. Photos in tools/results/.../scope-20261119-ampdip/.

### 2026-11-19 15:41:57 — Scope: amp dip, full sequence (manual note)

- 50 ms/div captures (whole power.ampdip sequence): no step at amp enable; the 1 kHz tone (-12 dBFS, ~100 ms, starting ~250 ms after AMP_SD rises) lowers the lower envelope of +V and VBAT by ~50 mV; device shows dip 75-79 mV. Narrow +-100 mV spikes at internal transitions. Correction to the earlier note: the 20 ms/div captures ended before the tone. F6 conclusion unchanged (no reset risk without the meter burden).
