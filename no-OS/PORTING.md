# Porting a no-OS project to libiio

How to move a no-OS project from `iio_app` to libiio's iiod. After the port
there is one IIO implementation, libiio: no-OS's `iio_app`, `iio/iio.c`,
`iiod.c` and `iio_trigger.c` are not built at all (the build fails if any of
them is).

Devices are described as **tables**, the way a Linux IIO driver describes its
channels: a driver lists its channels with the standard macros and reads and
writes numbers; libiio names, formats and parses every attribute and packs the
samples (see "Describing a device"). A no-OS driver that is not converted yet
still works unmodified through a bridge (step 3).

Nothing about the board is fixed inside libiio: every uart, timer, network
and usb setting comes from the project, the way it does with `iio_app` today.

## What changes in a project

| | with `iio_app` | with libiio |
|---|---|---|
| Kconfig | `CONFIG_IIO=y` | `CONFIG_IIO` off |
| CMake | nothing (iio_app is in `no-os`) | `CONFIG_LIBIIO`: no-OS includes libiio's `module.cmake`, which links `libiio-iiod` |
| devices | `struct iio_app_device[]` built in the example's code | `struct noos_iiod_device[]` as data, in `common_data.c` |
| driver init | the example calls `xxx_init()` itself | the table row names `xxx_init` and its init param; libiio calls it |
| settings | `iio_app_init_param` fields | `struct noos_iiod_param` (same no-OS init-param structs) |
| `main()` | example main: init, `iio_app_init()`, `iio_app_run()` | `return noos_iiod_run(&iiod_param);` |

The project's init parameters are reused unchanged; drivers either are
table devices or go through the bridge unmodified.

## Describing a device

A device is a `struct noos_iio_device_info` (`include/iio_device.h`) with a
table of channels and a few callbacks. The standard channels of each type are
macros in `include/iio_channels.h`, so every part of a type looks the same to a
host:

```c
#include <iio_channels.h>

static const struct noos_iio_chan accel_channels[] = {
	/* x, y, z as scan elements 0..2: raw each, scale and sampling
	 * frequency shared by type, 20-bit signed in 32-bit LE storage */
	NOOS_IIO_ACCEL_XYZ(NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(SCALE) |
			   NOOS_IIO_BIT(SAMP_FREQ), NOOS_IIO_BIT(SAMP_FREQ),
			   { .sign = 's', .realbits = 20, .storagebits = 32,
			     .endianness = NOOS_IIO_LE }),
	NOOS_IIO_TEMP_CHANNEL(-1, NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(SCALE), 0,
			      NOOS_IIO_NO_SCAN_TYPE),
};

static int accel_read_raw(void *dev, const struct noos_iio_chan *ch,
			  enum noos_iio_info info, int *val, int *val2)
{
	switch (info) {
	case NOOS_IIO_INFO_RAW:
		return my_part_read_axis(dev, ch->priv, val) ?: NOOS_IIO_VAL_INT;
	...
	}
}

*info = (struct noos_iio_device_info) {
	.name = "my-part",
	.dev = desc,
	.channels = accel_channels,
	.num_channels = NO_OS_ARRAY_SIZE(accel_channels),
	.read_raw = accel_read_raw,
	.write_raw = accel_write_raw,		/* NULL: read-only */
	.read_avail = accel_read_avail,		/* for the _available bits */
	.read_scan = accel_read_scan,		/* one value per enabled channel */
	.trigger = "timer0",			/* what paces the buffer */
};
```

From this libiio makes, with Linux's names: `in_accel_x_raw`,
`in_accel_scale`, `in_accel_sampling_frequency` and its `_available`,
`in_temp_raw`, ...; values come back as numbers (`NOOS_IIO_VAL_INT`,
`_INT_PLUS_MICRO`, `_FRACTIONAL_LOG2`, ...) and are printed and parsed by
libiio. Fixed data (a constant scale, a frequency picked from a list) can be a
`struct noos_iio_value` (`NOOS_IIO_CONST()`, `NOOS_IIO_LIST()`) instead of
code; a setting with names is a `struct noos_iio_enum` (`NOOS_IIO_ENUM()`).
Buffered capture calls `read_scan` once per tick of `.trigger` and packs the
values with each channel's `scan_type`. Registration checks the tables and
refuses a wrong one with `-EINVAL`.

The MCU's own ADC is such a device, from the platform's glue
(no-OS `drivers/platform/maxim/common/maxim_iio_adc.c`, `CONFIG_IIO_ADC_MAXIM`):
one table row, `NOOS_IIOD_ONCHIP_ADC()`.

## Step by step

Worked example: no-OS's `projects/iio_demo`, its "Basic example served by
libiio" (`CONFIG_IIO_DEMO_LIBIIO_EXAMPLE`, `src/examples/libiio_example/`,
the table at the end of `src/common/common_data.c`). Variants
`libiio.conf`, `libiio_usb.conf` and `libiio_network.conf` serve
`adc_demo`, `dac_demo`, the part's ADC and `timer0` over uart, usb and TCP on
the AD-APARD32690-SL:

```
cmake -B build --preset ad-apard32690-sl -DPROJECT_DEFCONFIG=iio_demo/libiio.conf
cmake --build build --target iio_demo
cmake --build build --target flash
```

### 1. Kconfig

In the project's `.conf`: `CONFIG_IIO` off, `CONFIG_LIBIIO=y` and one of
`CONFIG_LIBIIO_TRANSPORT_UART` / `_USB` / `_NETWORK`
(`CONFIG_LIBIIO_V0_COMPAT` for libiio v0.x clients: off by default, it needs
libiio's `tinyiiod/ascii-ops.c`). Keep the
driver options (`CONFIG_ADC_DEMO=y`, ...) and the transport's no-OS options:
`CONFIG_UART`, `CONFIG_TIMER`, `CONFIG_IRQ`; for network `CONFIG_NET`,
`CONFIG_SPI`, `CONFIG_DMA` and the PHY (`CONFIG_ADIN1110`); for usb on Maxim
`CONFIG_USB_UART_MAXIM` (MAXUSB). `CONFIG_IIO_ADC_MAXIM` adds the part's own
ADC. `iio_demo`'s `libiio*.conf` and `boards/libiio*/` show a full set.

### 2. CMake

Nothing in the project's `CMakeLists.txt`. With `CONFIG_LIBIIO`, no-OS's
`cmake/libraries/libiio.cmake` finds libiio the way `lwip.cmake` finds lwIP
(`NO_OS_DEP_LIBIIO_PATH` to point at a local tree, else a clone of
`CONFIG_LIBIIO_VERSION`) and includes its `no-OS/module.cmake`, which builds
`libiio-iiod` (`libiio_noos_add_server()`) and links it into the project.

`libiio-iiod` is an OBJECT library, like `no-os`, so it carries libiio's
objects straight into the executable. The executable keeps linking `no-os`
as every no-OS project already does.

### 3. Device table, in `common_data.c`

A table device takes a plain row: `.init` brings it up with `.param`,
`.get_info` gives its `struct noos_iio_device_info`. The port's own devices
have row macros:

```c
static const struct noos_iiod_device iiod_devices[] = {
	NOOS_IIOD_ONCHIP_ADC("timer0"),		/* the MCU's ADC, paced by timer0 */
	NOOS_IIOD_TIMER_TRIGGER(&trigger_timer),	/* struct noos_iiod_timer */
	{ .init = my_part_init, .param = &my_part_ip, .get_info = my_part_info },
};
```

A no-OS driver not converted to a table yet (`iio_<part>.c` with a no-OS
`struct iio_device`) goes through the bridge, unmodified: one
`NOOS_IIOD_DRIVER_DEFINE()` and one row.
The macro arguments are what the example used to pass to the driver's init
and to `IIO_APP_DEVICE()`:

```c
#include <noos_iiod.h>
#include <iio_noos_driver.h>
#include "iio_adc_demo.h"
#include "iio_dac_demo.h"

/* adc_init_par, dac_init_par: the project's existing init parameters. */

NOOS_IIOD_DRIVER_DEFINE(adc_demo, "adc_demo", adc_demo_init,
			struct adc_demo_desc, &adc_init_par,
			&adc_demo_iio_descriptor, NULL);
NOOS_IIOD_DRIVER_DEFINE(dac_demo, "dac_demo", dac_demo_init,
			struct dac_demo_desc, &dac_init_par,
			&dac_demo_iio_descriptor, NULL);

static const struct noos_iiod_device iiod_devices[] = {
	NOOS_IIOD_DRIVER(adc_demo),
	NOOS_IIOD_DRIVER(dac_demo),
};
```

| `IIO_APP_DEVICE(...)` argument | becomes |
|---|---|
| `"adc_demo"` (name) | 2nd macro argument |
| `adc_desc` (instance) | filled in by libiio from the driver's init |
| `&adc_demo_iio_descriptor` | last macro argument |
| `&adc_buff` / `&dac_buff` (`read_buff`/`write_buff`) | not needed: libiio allocates the buffer blocks |
| `default_trigger_id` | last macro argument: the trigger's name (`"timer0"`), or `NULL` |


### 3b. Triggers

A no-OS trigger takes one `NOOS_IIOD_TRIGGER_DEFINE()` and one row, built
from what the example used to pass to `iio_hw_trig_init()` and
`IIO_APP_TRIGGER()`. libiio brings up the interrupt (and timer), creates the
trigger and starts the timer; the example's `no_os_timer_init()`,
`no_os_irq_ctrl_init()`, `iio_hw_trig_init()` and `no_os_timer_start()`
calls go away:

```c
#include "iio_trigger.h"

/* adc_trig_ip (struct iio_hw_trig_init_param, irq_ctrl left NULL),
 * timer_irq_ip and adc_timer_ip: the project's existing parameters. */

NOOS_IIOD_TRIGGER_DEFINE(adc_timer_trig, "adc_demo_timer_trig",
			 &adc_iio_timer_trig_desc, &adc_trig_ip, &timer_irq_ip,
			 &adc_timer_ip, 3);
NOOS_IIOD_TRIGGER_DEFINE(adc_sw_trig, "adc_demo_sw_trig",
			 &adc_iio_sw_trig_desc, NULL, NULL, NULL, 0);

NOOS_IIOD_DRIVER_DEFINE(adc_demo, "adc_demo", adc_demo_init,
			struct adc_demo_desc, &adc_init_par,
			&adc_demo_iio_descriptor, "adc_demo_timer_trig");

static const struct noos_iiod_device iiod_devices[] = {
	NOOS_IIOD_DRIVER(adc_demo),
	NOOS_IIOD_TRIGGER(adc_timer_trig),
	NOOS_IIOD_TRIGGER(adc_sw_trig),
};
```

| `IIO_APP_TRIGGER(...)` / `iio_hw_trig_init()` | becomes |
|---|---|
| trigger name | 2nd macro argument (drivers name it as their trigger) |
| `&adc_iio_timer_trig_desc` (the driver's `*_trig.c`) | 3rd argument |
| `struct iio_hw_trig_init_param` | 4th argument; libiio fills `irq_ctrl`. `NULL`: software trigger |
| `no_os_irq_ctrl_init()` param | 5th argument |
| `no_os_timer_init()` param | 6th argument; `NULL` for a GPIO trigger |
| `no_os_irq_set_priority()` | last argument |
| `trig->iio_desc = app->iio_desc` | not needed |

Each trigger is a libiio trigger device. A device starts on the trigger its
`NOOS_IIOD_DRIVER_DEFINE()` names; the host can pick another, or none
(`iio_rwdev -t <trigger>`). A software trigger fires on `trigger_now`
writes (`iio_attr -d <trigger> trigger_now 1`).

The drivers' `*_trig.c` files are compiled unchanged: `iio_trig_enable()`,
`iio_trig_disable()`, `iio_hw_trig_init()`, `iio_sw_trig_handler()` and the
rest are provided by this port under no-OS's names, so no-OS's
`iio_trigger.c` is not built.

### 4. Settings, in `common_data.c`

```c
const struct noos_iiod_param iiod_param = {
	.devices = iiod_devices,
	.nb_devices = NO_OS_ARRAY_SIZE(iiod_devices),
	.uart = &iio_demo_uart_ip,	/* uart transport */
	/* .lwip = &lwip_ip,		   network: struct lwip_network_param */
	/* .usb = &usb,			   usb: struct noos_iiod_usb */
	/* .console = &uart_ip,		   usb/network: where logs go */
};
```

Only the fields of the transport being built are read. All of them are the
no-OS init-param structs the project already has:

| field | type | used on |
|---|---|---|
| `uart` | `struct no_os_uart_init_param *` | uart (libiio sets `asynchronous_rx`) |
| `lwip` | `struct lwip_network_param *` (MAC/PHY ops, MAC address) | network |
| `usb` | `struct noos_iiod_usb *`: tick timer + usb ids and strings | usb |
| `console` | `struct no_os_uart_init_param *`, optional | usb, network |

`struct noos_iiod_timer` (in `include/noos_iiod.h`) is a no-OS timer init
param plus its interrupt: controller, IRQ number, handle, priority. Give all
timers the same priority: no-OS walks every timer callback with one shared
iterator.

### 5. `main()`

```c
#include <noos_iiod.h>

extern const struct noos_iiod_param iiod_param;

int main(void)
{
	return noos_iiod_run(&iiod_param);
}
```

On bare metal this one call is the minimum: no-OS has no threads and no
boot-time init hooks, so nothing else can start iiod (on Zephyr,
`K_THREAD_DEFINE` does it and `main()` is empty).

### 6. Check

```
iio_info -u serial:/dev/ttyACM0,115200     # or usb:, ip:<address>
```

Every driver of the table is listed. A device whose init fails is logged on
the console and left out; the others are still served.

## Not supported yet

| `iio_app` feature | status |
|---|---|
| trigger handler in interrupt context (`is_synchronous`) | the interrupt counts a tick; `trigger_handler` runs from the capture loop, one call per tick |
| context attributes (`ctx_attrs`) | missing |
| `post_step_callback` | missing: iiod blocks while serving |
| project-supplied buffer memory (`read_buff`/`write_buff`) | not used; blocks are allocated |
| WiFi (ESP8266) and W5500 networking | missing: only lwIP |
| buffer attributes from a descriptor | missing |
| the usb transport on a platform other than Maxim | needs that platform's glue behind `iio_usb_hal.h` (no-OS `maxim_iio_usb.c` is the example) |

## Known limits

- One transport per image, chosen at build time.
- Network serves one client at a time.
- Serial cannot resync after a client dies mid-transfer: reset the board.
- A buffer transfer blocks the board until done (enqueue is synchronous).
  Over serial, a triggered transfer that takes long while the host sends
  the next blocks overflows the 256-byte uart receive FIFO: use usb or
  network for long triggered output streams, or one block at a time.
- A no-OS trigger that does not tick for 5 s ends the capture with
  `-ETIMEDOUT`. A software-triggered capture can only use `trigger_now`
  writes made before it starts.
- Only the AD-APARD32690-SL has run on hardware, with `iio_demo`'s libiio
  variants built by no-OS.

## What each file is

In libiio's `no-OS/` (the library):

| file | purpose |
|---|---|
| `module.cmake` | what no-OS includes with `CONFIG_LIBIIO`: builds and links `libiio-iiod` |
| `cmake/libiio_noos_server.cmake` | `libiio_noos_add_server()`: the `libiio-iiod` OBJECT library |
| `backend.c` | the libiio backend (`no-os:`): builds the context from the registered devices, routes attributes, buffers, registers and the trigger choice to them |
| `backend_table.c` | the table layer: a table device's channels and attributes, value formatting and parsing, sample packing; enum attributes and value helpers |
| `iiod/uart.c`, `network.c`, `usb.c` | transports: move bytes between the host and libiio's responder; one is built |
| `include/noos_iiod.h` | the project's API: `noos_iiod_run()`, device table, settings, `NOOS_IIOD_ONCHIP_ADC()` / `NOOS_IIOD_TIMER_TRIGGER()` |
| `include/iio_channels.h` | standard channels for table devices: ADC inputs, accelerometers, temperature |
| `include/iio_device.h` | how a device plugs into `backend.c`: `noos_iio_device_info`, the table types |
| `include/iio_noos_driver.h` | serving an unconverted no-OS driver or trigger: `NOOS_IIOD_DRIVER_DEFINE()`, `NOOS_IIOD_TRIGGER_DEFINE()` |
| `include/iio_usb_hal.h` | contract between `iiod/usb.c` and a platform's usb glue |
| `include/iio-config.h`, `include/compat/sys/socket.h` | shims so upstream libiio and its v0 parser compile on bare metal |
| `drivers/iio_noos_driver.c` | libiio side of the bridge for unconverted no-OS drivers and triggers |
| `drivers/iio_noos_driver_desc.c/.h` | no-OS side of the bridge (reads the no-OS descriptor; provides `iio_buffer_*`, `iio_trig_*`, `iio_hw_trig_*`); separate because no-OS and libiio both define `struct iio_device` |
| `drivers/iio_trigger_timer.c/.h` | `timer0` trigger on a project-given hardware timer |
| `ARCHITECTURE.md` | internals |

In no-OS (branch adding `CONFIG_LIBIIO`):

| file | purpose |
|---|---|
| `libraries/Kconfig`, `libraries/CMakeLists.txt`, `cmake/libraries/libiio.cmake` | `CONFIG_LIBIIO`, transport choice, finding libiio |
| `drivers/platform/maxim/common/maxim_iio_adc.c/.h` | the part's own ADC as a table device (`CONFIG_IIO_ADC_MAXIM`) |
| `drivers/platform/maxim/common/maxim_iio_usb.c` | MAXUSB glue behind libiio's usb transport |
| `projects/iio_demo` | the worked example: `libiio_example`, `libiio*.conf`, settings in `src/platform/maxim/parameters.*` |
