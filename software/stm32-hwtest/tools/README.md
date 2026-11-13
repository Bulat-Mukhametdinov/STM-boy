# stm32-hwtest host tools

`hwtest.py` is the host side of [`../PROTOCOL.md`](../PROTOCOL.md). It needs Python 3.9+ and
pyserial. On this PC, run it with `py -3.14`. Flashing also needs `STM32_Programmer_CLI` on PATH
(STM32CubeCLT).

```
py -3.14 tools/hwtest.py ports                  # CDC ports (0483:5740) + DFU devices (0483:DF11)
py -3.14 tools/hwtest.py info                   # device info JSON
py -3.14 tools/hwtest.py cmd w25q id            # raw command, prints lines until @OK/@ERR/@DONE
py -3.14 tools/hwtest.py monitor                # print everything, reconnects after resets
py -3.14 tools/hwtest.py run selftest           # run a suite, results -> tools/results/<uid>/...
py -3.14 tools/hwtest.py dfu                    # app -> ST DFU bootloader ('dfu' cmd, else 1200-baud touch)
py -3.14 tools/hwtest.py flash build/hwtest.bin # DFU flash + verify fw/build against build/fw_id.json
py -3.14 tools/hwtest.py dfu-test --cycles 20   # DFU reliability test (automatic part)
py -3.14 tools/hwtest.py dfu-test --manual      # BOOT0 path + recovery rehearsal (interactive)
py -3.14 tools/test_hwtest.py                   # unit tests with a simulated device
```

Common options: `--port COMx`, `--serial <usb serial>` (env `HWTEST_PORT` / `HWTEST_SERIAL`),
`--json` (one JSON summary on stdout; human text goes to stderr), `--wait-lock [s]`,
`--log PATH` / `--no-log`.

Exit codes: `0` ok/pass, `1` fail, `2` abort/timeout/no device/error, `3` board locked.

## Prompts

`run` prints a banner for every `@P` line (`ON DEVICE: <text> — answer with buttons A/B`) and
keeps waiting. The idle timeout (`--timeout`, the time allowed without any device line) is
paused while a prompt is open. Instead, `--prompt-timeout` limits how long a prompt can stay
unanswered (600 s by default). With `--answer-from-host`, you can also type `a`/`ok` or
`b`/`fail` on the host. The tool then sends `answer <id> ok|fail`, which the firmware accepts
since P1 (it acts like pressing A / B on the device). Use it only when a human is actually
looking at the device.

W25Q dumps: `py -3.14 tools/hwtest.py cmd w25q read 100000 65536 --save-bin DIR`; long
`w25q crc` / `w25q scan` runs need `--timeout 120`.

## Board lock

Every command that talks to the board takes `tools/.board.lock` (pid, command, time). A lock is
stale when its pid is no longer alive. `lock-status` shows who holds the lock. The lock file
sits inside this checkout, so agents working in other git worktrees do not see it. To share one
lock between worktrees, point `HWTEST_LOCK` at the same file in every worktree.

## Hardware log

Every hardware-touching action is appended to `../HW_LOG.md` as an entry tagged
`### <time> — <action> [auto]`. Logged actions are each invocation, each DFU trigger, each
STM32_Programmer_CLI call (command line, exit code, key output lines), each run start and end,
each dfu-test cycle, lock waits and errors. The file is append-only. Override its location with
`--log` or `HWTEST_LOG`, or turn logging off with `--no-log`.

## Safety

All STM32_Programmer_CLI calls go through `check_cli_args_safe`. It refuses erase (`-e`),
`-rdu`, raw word writes, and any `-ob` other than `-ob displ`, and it requires `sn=`. Option
bytes are never written, and the tool never mass-erases the chip.

Result files land in `tools/results/` (consider adding it to `.gitignore`).
