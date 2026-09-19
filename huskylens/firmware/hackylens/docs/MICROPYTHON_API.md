---
contract-id: hackylens.micropython-api
owner: micropython-runtime
version: 1.0.0
stability: experimental
api-major: 1
---

# HackyLens MicroPython API v1

HackyLens embeds upstream MicroPython inside the main firmware. A program runs
on K210 core 1 with a private 128 KiB GC heap and a 24 KiB conservative stack
limit. Core 0 keeps the menu, serial protocol, display, flash, and hardware
services responsive. The default execution limit is 30 seconds and the maximum
requested limit is 300 seconds.

The native **MICRO-PYTHON** app is a script manager. It lists USERFS `.py`
files, previews source without loading the whole file, runs the selected file,
shows scrollable output, changes startup selection, and deletes only after an
explicit confirmation. Opening the app does not run the selected startup file.
That selection remains the HMPY/default program used when `RUN` omits a name.

Import the board module with:

```python
import hackylens as hl
```

## Buttons and time

| API | Result |
|---|---|
| `buttons()` | current debounced button bit mask from `hackylens.cap.input` |
| `button(mask)` | `True` if any bit in `mask` is pressed |
| `ticks_ms()` | monotonic milliseconds since boot |
| `sleep_ms(duration)` | cooperative sleep; `0..300000` ms |

Button constants are `BUTTON_LEFT = 1`, `BUTTON_OK = 2`,
`BUTTON_RIGHT = 4`, and `BUTTON_BACK = 8`. `sleep_ms()` checks both a remote
stop request and the run deadline at least every 5 ms.

## Display

The releaseable SEN0305 runtime currently selects a 320 by 240 display through
descriptor-generated defaults. Colors are unsigned RGB565 values from `0x0000`
through `0xffff`.

| API | Meaning |
|---|---|
| `display_clear(color=0)` | fill the complete display |
| `display_text(x, y, text, foreground=0xffff, background=0)` | draw up to 256 encoded bytes |
| `display_rect(x, y, width, height, color, filled=False)` | draw an outline or filled rectangle |
| `display_present()` | atomically make the currently staged display list visible |

`display_clear()`, `display_text()`, and `display_rect()` only stage a bounded
frame; the panel is updated by `display_present()`. `display_clear()` starts a
fresh staged frame, while a successful present replaces the previous Python
overlay. Coordinates must start on screen; rectangles and text are clipped at
the right and bottom edges. Invalid dimensions, oversized text, and a command
or text buffer overflow raise `ValueError`. Script cleanup removes the overlay
and restores the latest firmware-owned screen.

## Illumination and RGB LED

| API | Meaning |
|---|---|
| `led(brightness)` | white illumination LED, `0..100`; zero turns it off |
| `rgb(red, green, blue)` | RGB status LED, each channel `0..255`; all zero turns it off |

The script temporarily owns any light it changes. When the program completes,
is stopped, times out, or raises an exception, firmware reapplies the persisted
light settings.

## External UART

UART and I2C use the descriptor-selected HUSKYLENS external connector routes.
The script claims that connector on its first UART or I2C call; the normal
HackyLens external-link service is suspended until script cleanup.

| API | Meaning |
|---|---|
| `uart_init(baud=115200)` | select UART at `1200..2000000` baud |
| `uart_write(data)` | transmit `str` or `bytes`; returns the byte count |
| `uart_read(size=64)` | return up to `0..256` currently buffered bytes |

`uart_write()` transparently splits larger values into 256-byte service calls.
`uart_read()` is non-blocking and may return `b""`.

## External I2C

The v1 I2C binding is a 100 kHz controller with 7-bit addresses.

| API | Meaning |
|---|---|
| `i2c_write(address, data)` | write up to 256 bytes to address `1..127` |
| `i2c_read(address, size, prefix=b"")` | optional prefix write, then read `0..256` bytes |

An I2C transaction has a 100 ms hardware deadline. A NACK or bus error raises
`OSError`; a service deadline raises `RuntimeError`. The connector mode and
normal external-link service are restored after every run.

## Example

```python
import hackylens as hl

hl.display_clear(0x0000)
hl.display_text(8, 8, "MicroPython ready", 0xffff, 0x0000)
hl.display_present()
hl.rgb(0, 64, 0)

last_ok = False
while not hl.button(hl.BUTTON_BACK):
    ok = hl.button(hl.BUTTON_OK)
    if ok:
        hl.led(60)
    else:
        hl.led(0)
    if ok != last_ok:
        hl.display_clear(0x0000)
        hl.display_text(8, 8, "MicroPython ready", 0xffff, 0x0000)
        if ok:
            hl.display_rect(8, 40, 80, 30, 0x07e0, True)
        hl.display_present()
        last_ok = ok
    hl.sleep_ms(20)
```

## Stop and cleanup contract

The VM checks stop/deadline hooks at Python branch and return points, in both
central iterator gateways used by native builtins such as `sum()` and
`min()/max()`, during cooperative sleep, and while waiting for a core-0 binding
call. A stop or deadline is delivered as `KeyboardInterrupt`; user code may
catch it briefly, but the request and deadline remain active. Every hardware
service call is bounded.

If core 1 has not finished two seconds after STOP, or five seconds after the
run deadline, core 0 starts WDT1 as a one-shot fatal fallback. The watchdog is
not armed during ordinary execution or flash mutation. Its hardware reset
cause suppresses firmware autostart for that boot; the native script manager
never starts a script on entry, and an explicit run remains available after
inspection. New
HMPY v1 clients can distinguish this recovery path through HELLO capability
`BOOT_FLAGS` and boot flag `WDT1_RECOVERY`; older v1 clients safely ignore the
formerly reserved byte.

After normal or cooperative execution, HackyLens deinitializes the VM, restores lights and the
external connector service, and reports the final state through the app and
HMPY protocol. The fixed heap and source buffers stay reserved for reuse by the
next run; no script allocation survives VM deinitialization. An uncaught
exception is reported as an error run and its traceback is sent through stdout.
The fatal watchdog path restores ownership by resetting the whole SoC because
asynchronously unwinding an arbitrary native C frame would be unsafe.
It has a dedicated `-wdtfi` firmware build and a two-phase read-only
baseline/post-reset acceptance procedure; that disruptive physical gate has not
yet been run. The production API cannot deliberately wedge core 1.
See [WDT1 hardware acceptance](WDT_HARDWARE_ACCEPTANCE.md).

Camera, KPU, SD/files, and vision-result bindings are intentionally outside API
v1 and remain a later roadmap increment.
