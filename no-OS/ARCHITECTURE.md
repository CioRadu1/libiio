# libiio on no-OS — architecture

How libiio v1.0 serves IIO devices on no-OS firmware: one IIO implementation
(libiio's iiod), no-OS's drivers and platform code underneath, no `iio_app`.
For moving a project over, see `PORTING.md`.

## 1. Layers

```
host (iio_info, iio_attr, iio_rwdev, Scopy)        libiio client, PC
──────────────────────────────── uart / usb / tcp ────────────────────────────
transport      iiod/{uart,usb,network}.c     bytes in/out, session loop
protocol       ../iiod/responder.c           v1 binary (+ v0 ASCII parser)
libiio core    ../context.c, buffer.c, ...   the IIO object model
backend        backend.c                     iio_backend_ops "no-os:"
devices        backend_table.c               table devices (Linux-style)
               drivers/iio_noos_driver*.c    bridge for unconverted no-OS drivers
               drivers/iio_trigger_timer.c   timer0 trigger
no-OS          drivers, platform, MSDK       no_os_uart/timer/irq/spi, chips
```

Each layer only calls the one below it. Everything under `../` is upstream
libiio, compiled unmodified.

## 2. Start-up

The project's `main()` is one call:

```c
return noos_iiod_run(&iiod_param);   /* include/noos_iiod.h */
```

`iiod_param` (in the project's `common_data.c`) holds the device table and the
transport settings, all as no-OS init-param structs. `noos_iiod_run()` (each
transport has one; one is built):

1. `noos_iiod_register()` (`backend.c`): points the logs at `.console` if set,
   then for each table row calls `init(param)` and `get_info()` and registers
   the device. A failing row is logged and left out.
2. opens the transport with the project's settings (uart, lwIP, usb).
3. `iio_create_context("no-os:")` — `noos_create_context()` builds one IIO
   device per registered one (`trigger%u` ids for triggers, `iio:device%u` for
   the rest), adds a buffer holding the scan elements, links each device to
   its info with `iio_device_set_pdata()`.
4. loops: wait for a client, run libiio's `iiod_interpreter()` until the
   session ends.

## 3. Device model

A device is a `struct noos_iio_device_info` (`include/iio_device.h`). Two
kinds, side by side in one image:

**Table devices** (the standard way). The device lists its channels as
`struct noos_iio_chan` (type, index, modifier, scan type, and which standard
attributes it has as `NOOS_IIO_BIT()` masks — separate, shared by type, by
direction, by all), and gives `read_raw` / `write_raw` / `read_avail` /
`read_scan`. `noos_iio_register_device()` calls `noos_iio_table_install()`
(`backend_table.c`), which checks the tables and fills in `add_channels`,
`read_attr`, `write_attr` and `read_samples`:

- attribute names and file names follow Linux (`in_voltage0_raw`,
  `in_accel_scale`, `sampling_frequency`, `*_available`);
- values travel as numbers (`NOOS_IIO_VAL_INT`, `_INT_PLUS_MICRO`,
  `_FRACTIONAL_LOG2`, ...) and are formatted/parsed with `noos_value_*`;
- fixed data is a `struct noos_iio_value` (`NOOS_IIO_CONST()`,
  `NOOS_IIO_LIST()`), named settings a `struct noos_iio_enum`;
- buffered capture: per scan, wait for the trigger, `read_scan()` gives one
  value per enabled channel, packed with each channel's scan type (alignment,
  endianness, sign, shift as `iio_device_get_sample_size()` lays them out).

`include/iio_channels.h` has the standard channels (ADC inputs, accelerometer
axes, temperature), so all parts of a type look alike. The MCU's own ADC is a
table device in no-OS's platform code (`maxim_iio_adc.c`), one row:
`NOOS_IIOD_ONCHIP_ADC()`.

**Bridged no-OS drivers** (migration). An unconverted `iio_<part>.c` with a
no-OS `struct iio_device` descriptor is served unmodified:
`NOOS_IIOD_DRIVER_DEFINE()` makes its row. `iio_noos_driver.c` (libiio side)
and `iio_noos_driver_desc.c` (no-OS side) are two files because both libraries
define `struct iio_device`; they talk through `iio_noos_driver_desc.h` in plain
types. What drivers call into no-OS's IIO server — `iio_buffer_push_scan()`,
`iio_buffer_get_block()`, ... — is provided by the bridge under the same names,
so no-OS's `iio.c` is never built.

## 4. Triggers

Every paced device names its trigger (`.trigger`); the host can pick another
(`iio_rwdev -t`, `noos_set_trigger()`). One wait serves all:
`noos_trigger_wait(name)` — a no-OS trigger of the table by name, else
`timer0`.

- `timer0` (`iio_trigger_timer.c`): a project-given hardware timer
  (`struct noos_iiod_timer`), `sampling_frequency` in Hz, one scan per tick;
  started on the first wait, stopped when the buffer is disabled.
- no-OS triggers (`NOOS_IIOD_TRIGGER_DEFINE()`): hardware (timer or GPIO
  interrupt, `iio_hw_trig_init()`) or software (`trigger_now`), from the
  drivers' own `*_trig.c`. The interrupt only counts a tick (`fired`); the
  capture loop consumes ticks (`seen`) and runs the driver's
  `trigger_handler` once per tick. No lock: each counter has one writer.
- a trigger that does not tick for 5 s ends the capture with `-ETIMEDOUT`.

## 5. Buffers

`open_buffer` stores the client's channel mask (owned by the stream, valid
until close). Blocks are `malloc`ed. `enqueue_block` does the whole transfer
inline and keeps its result; `dequeue_block` returns it. So capture and the
protocol never overlap: the board serves one request at a time.

## 6. Transports

- **uart** (`iiod/uart.c`): the project's `no_os_uart_init_param` (async rx
  forced on); one client at a time, no disconnect detection.
- **network** (`iiod/network.c`): lwIP with the project's
  `lwip_network_param`, port 30431, one client at a time; an idle client is
  dropped after 3 s so the next one (e.g. a stream's second socket) gets in.
- **usb** (`iiod/usb.c` + the platform's glue behind `include/iio_usb_hal.h`,
  `maxim_iio_usb.c` on Maxim): libiio's vendor interface, 2 bulk pipe pairs, a
  project-given tick timer for timeouts and keeping reads queued; ids and
  strings from the project.

## 7. Build and no-OS integration

no-OS takes libiio like lwIP: `CONFIG_LIBIIO` (`libraries/Kconfig`, transport
choice, v0 compat) → `cmake/libraries/libiio.cmake` resolves the libiio tree
(`NO_OS_DEP_LIBIIO_PATH` or a clone) and includes `no-OS/module.cmake` →
`libiio_noos_add_server()` (`cmake/libiio_noos_server.cmake`) builds the
`libiio-iiod` OBJECT library and it is linked into the project.

- OBJECT, like `no-os`: no-OS's strong IRQ handlers would drop out of a static
  archive; the project's executable links `no-os` itself.
- `CONFIG_IIO` must be off: the build stops otherwise.
- Defines: `WITH_LIBTINYIIOD`, `WITH_EXTERNAL_BACKEND`, `NO_THREADS`, all other
  backends off.
- Platform glue (on-chip ADC, usb) is no-OS's, under `CONFIG_LIBIIO`; no-OS
  also raises the startup stack (64 KiB) for the responder.

## 8. Limits

- one transport per image; one client at a time;
- synchronous transfers: a long triggered stream blocks the board, and over
  serial the host can overrun the 256-byte uart receive FIFO;
- serial cannot resync after a client dies mid-transfer (reset the board);
- the trigger handler runs from the capture loop, not in the interrupt;
- no events, refresh format or direct register access yet;
- attribute payloads are capped at 256 bytes (`READ_ATTR_BUF_SIZE` under
  `WITH_LIBTINYIIOD`).
