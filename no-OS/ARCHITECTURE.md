# libiio on no-OS — architecture

How libiio v1.0 is wired into the ADI no-OS framework, layer by layer, with
`file:line` citations so every claim can be re-verified. Line numbers drift; the
surrounding function names do not, so grep the name if a number no longer matches.

**Reference implementation.** The Zephyr port is the reference for this one — the
two are kept interface-compatible on purpose. The live Zephyr tree is
**`~/RaluZephyr/zephyr/`**. The `zephyr/` directory checked into this repo is a
**stale snapshot**: its `backend.c` is 104 lines with a 4-op ops table and no
buffer support at all. Reading that copy and concluding "Zephyr cannot do X" is
wrong. See §14.

---

## 1. What libiio v1.0 expects from a backend

A backend is a `struct iio_backend` wrapping a `struct iio_backend_ops` table
(`include/iio/iio-backend.h`). The build defines `WITH_EXTERNAL_BACKEND=1`
(`CMakeLists.txt:109`), which makes libiio reference an undefined symbol
`iio_external_backend` and lets the linker pull in whatever provides it. This port
provides it at `backend.c:350-356`:

```c
const struct iio_backend iio_external_backend = {
	.name = "no-os",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 0,
	.uri_prefix = "no-os:",
	.ops = &noos_ops,
};
```

So `iio_create_context(&params, "no-os:")` reaches `noos_create_context`.

Errors are encoded into pointers, kernel-style: `iio_ptr(-ENOMEM)` to return an
error as a pointer, `iio_err(ptr)` to extract it (0 means a real pointer),
`iio_err_cast(ptr)` to forward one. Any op returning a pointer uses this.

## 2. Layer map

```
transport     iiod/{uart,network,usb}.c     bytes in/out, handshake, main loop
protocol      iiod/responder.c              v1 binary opcode dispatch
backend       backend.c                     iio_backend_ops -> callback table
device class  drivers/iio_adc.c             ADC IIO semantics, attrs, formatting
silicon       drivers/adc/<part>/<part>.c    struct iio_adc_hal definition
```

Each layer knows only the one below it. Concretely, and worth preserving:
`backend.c` contains no ADC knowledge, `drivers/iio_adc.c` contains no `MXC_*`
call, and the part drivers contain no libiio call. That is why adding a board means
adding one file under `drivers/adc/` and nothing else.

## 3. Context construction

`noos_create_context` (`backend.c:266-325`):

1. `iio_context_create_from_backend(params, &iio_external_backend,
   NOOS_BACKEND_VERSION, 1, 0, 0, "v1.0")`
2. for each entry in `noos_iio_devices[]`:
   - the host-visible id: `"trigger%u"` when `info->is_trigger`, else
     `"iio:device%u"`, each with its own counter. The prefix is not cosmetic:
     `iio_device_is_trigger()` (`device.c`) requires an id starting with
     `trigger` **and** zero channels, and `iio_rwdev`/`iio_info` rely on it
   - `iio_context_add_device(ctx, id, info->name, NULL)`
   - `iio_device_set_pdata(iio_dev, (struct iio_device_pdata *)info)` — **this is
     the load-bearing line.** Every later op recovers its callback table with
     `iio_device_get_pdata(dev)`; there is no other link from an `iio_device` back
     to the no-OS device.
   - `info->add_channels(info->dev, iio_dev)` if present; a failure is logged
     with `pr_err` and the device is kept (a trigger uses the same hook to add
     its device attributes)
   - if the device ended up with channels: `iio_device_add_buffer(iio_dev, 0)`,
     `iio_buffer_set_direction(buf, info->direction ? "out" : "in")`, then one
     `iio_buffer_add_scan_element(buf, chn, NULL)` per channel for which
     `iio_channel_is_scan_element(chn)` holds.

The scan-element loop is not optional. Scan elements became buffer-owned upstream,
and without registering them the buffer reports zero of them, which is what
`iio_rwdev` uses to compute the sample size — it fails with a trigger timeout or
zero-length reads. `NULL` is passed for `en_path` because there is no sysfs enable
file on bare metal. The Zephyr port does the same thing at `backend.c:129-136`.

## 4. Device registry

`include/iio_device.h`. A device is described by value:

```c
struct noos_iio_device_info {
	const char *name;
	void *dev;
	int direction;                          /* 0 = input (RX), 1 = output (TX) */
	noos_iio_add_channels_t add_channels;
	noos_iio_read_attr_t read_attr;
	noos_iio_write_attr_t write_attr;
	noos_iio_read_samples_t read_samples;
	noos_iio_write_samples_t write_samples;
	noos_iio_enable_buffer_t enable_buffer;
	noos_iio_reg_read_t reg_read;
	noos_iio_reg_write_t reg_write;
	bool is_trigger;                        /* id "triggerN", no channels */
	const char *trigger;                    /* name of the trigger pacing it */
};
```

Fill it with a designated initializer (every driver in the tree does,
`*info = (struct noos_iio_device_info) { ... }`), so fields a device does not
use are zero.

The callback typedefs (`:26-58`) mirror Zephyr's `iio_device_driver_api` but take a
plain `void *dev` where Zephyr takes `const struct device *`. The sample path
carries the channel information Zephyr's `readbuf` gets:

```c
typedef int (*noos_iio_read_samples_t)(void *dev,
		const struct iio_device *iio_device,
		const struct iio_channels_mask *mask, void *data, size_t bytes);
typedef int (*noos_iio_enable_buffer_t)(void *dev, bool enable);
```

`mask` is the buffer's enabled set (§6), `bytes` a whole number of scans.
`enable_buffer` is optional and is called only on a real state change.

`noos_iio_register_device()` (`backend.c:186-196`) copies the struct into
`noos_iio_devices[NOOS_IIO_MAX_DEVICES]` (16) and bumps `noos_iio_device_count`.
Registration must happen **before** `iio_create_context`, because the context is
built from a snapshot of that table. `samples/iiod/main.c:noos_register_devices`
registers `iio-adc`, then the `timer0` trigger, then the snake game.

Still not carried: events.

## 5. Op-by-op mapping

`noos_ops` (`backend.c:327-348`) fills 16 of the `iio_backend_ops` slots.

| op | function | behaviour | gap |
|---|---|---|---|
| `create` | `:267` | see §3 | — |
| `read_attr` | `:199` | dispatch to `info->read_attr` | — |
| `write_attr` | `:212` | same shape | — |
| `get_trigger` | `:225` | `iio_context_find_device(ctx, info->trigger)`, `-ENODEV` when the device names none | `set_trigger` (`:247`) accepts only the trigger `get_trigger` returns (`-EINVAL` otherwise): the pairing is fixed at build time |
| `open_buffer` | `:34` | allocates `struct iio_buffer_pdata { dev; mask; enabled; }` and **stores the mask** (§6) | — |
| `close_buffer` | `:72` | disables the buffer first if the client never did (dropped connection), then `free` | — |
| `enable_buffer` | `:55` | calls `info->enable_buffer` on a real state change, tracks `enabled` | — |
| `cancel_buffer` | `:81` | empty | in-flight transfers cannot be aborted |
| `readbuf` / `writebuf` | `:85` / `:105` | `read_samples` / `write_samples` with `(info->dev, dev, mask, data, len)` | — |
| `create_block` / `free_block` | `:127` / `:147` | zalloc the pdata + malloc the payload | — |
| `enqueue_block` | `:153` | **performs the whole transfer inline**, with the mask, and stashes the result in `pdata->error` | not async |
| `dequeue_block` | `:181` | `return pdata->error;` | ignores `nonblock`, no `-EBUSY`, no double-op detection |
| `reg_read` / `reg_write` | `:242` / `:254` | `-ENOSYS` when the device hook is NULL | — |

Ordering worth knowing: `iio_stream` (and so `iio_rwdev`) enqueues its blocks
**before** it enables the buffer. Because enqueue is synchronous here, the first
blocks are filled while `enabled` is still false — so nothing may depend on
`enable_buffer(true)` having run first. The trigger therefore starts lazily on
the first sample wait, and stops on `enable_buffer(false)` (§7).

The libiio contract these deviate from, for when the async work is picked up:
enqueue is documented as asynchronous (`iio.h:1291-1310`);
`dequeue_block(nonblock=true)` should return `-EBUSY` when the block is not ready
(`iio.h:1308-1310`); double enqueue or double dequeue conventionally returns
`-EPERM` (`local-mmap.c:191-194`, `:228-231`). Today enqueue-then-dequeue works because the
transfer completes inside enqueue, so the deviation is invisible to a
well-behaved client and fatal to one that polls.

## 6. Channel masks

```c
struct iio_channels_mask {
	size_t words;
	uint32_t mask[];
};
```
(`iio-private.h:185-188`)

`iio_channel_is_enabled(chn, mask)` (`channel.c:503-506`) tests bit `chn->number`.
`number` is the channel's index in `dev->channels[]` **after sorting**
(`device.c:565`) — not its id, and not the `long index` passed to
`iio_device_add_channel`. Resolving a mask bit therefore means walking
`iio_device_get_channel(dev, i)` in order.

The mask handed to `open_buffer` is a private copy owned by the `buf_stream`:
allocated at `buffer.c:145-153`, passed at `:167`, freed only in
`iio_buffer_close` (`:196`). Consequences:

- the pointer is stable for the buffer's lifetime, so a backend **may store it**
  without copying — this is what the Zephyr port does, and what `backend.c`
  now does
- it is fully populated before `open_buffer` is called
- it is non-`const` on purpose: a backend may write bits back for channels the
  hardware forces on, e.g. coupled pairs (`local.c:1763-1773`)
- changing the enabled set requires close + reopen

For bytes-per-scan, use `iio_device_get_sample_size(dev, mask)`
(`device.c:368-407`) rather than hand arithmetic: it uses `format.length` (the
storage size) not `format.bits` (the meaningful bits), collapses channels sharing
an `index`, aligns each element to its own length, and pads the total.

## 7. `drivers/iio_adc.c` — the ADC device class

Per-channel state, the analogue of Zephyr's
`iio_device_io_channels_channel_adc_overrides`:

```c
struct adc_channel_state {
	int scale_val;
	int scale_val2;
	unsigned int gain;
	unsigned int reference;
	int differential;
};
static struct adc_channel_state chan_state[IIO_ADC_MAX_CHANNELS];
```
(`:56-64`)

`iio_adc_add_channels` (`:343`) registers, per channel, six attributes —
`raw`, `scale`, `gain`, `process`, `reference`, `differential` — plus one device
attribute `internal_ref_voltage`. It refuses a HAL declaring more than
`IIO_ADC_MAX_CHANNELS`. Channels are added with
`iio_device_add_channel(iio_dev, (long)i, name, NULL, NULL, false, true, &fmt)`:
not output, **is** a scan element, with a format built per channel (below).

### Where the attributes come from — do not re-litigate this

`gain_values[]` (21 entries) and `reference_values[]` (7 entries) at `:17-51` are
**copied from the Zephyr port on purpose**, added by commit `23b7d924`. They are
indexed by Zephyr's `enum adc_gain` and `enum adc_reference` from
`zephyr/include/zephyr/drivers/adc.h:42-64`.

So 21 and 7 are **the Zephyr ADC API's complete enumerations, not MAX32690
numbers.** They are a *namespace*, not a capability list: their job is to give
every possible setting one agreed name and one agreed number, so that
`gain = "1/6"` means the same thing to a host talking to either port. The `1/6`…`1/2`
divider gains come from parts like the Nordic SAADC; the `×2`…`×128` steps from
sigma-delta parts with a PGA. No single part implements more than a handful.

They must therefore **not** be shrunk to match one part's silicon: renumbering
would make `reference = 4` mean "Internal" on Zephyr and something else here, and
the same host tool would misconfigure one of the two ports.

What restricts writes to real hardware is the HAL hook, not the table — §8.

Since these indices are shared between this file and the part drivers, they are
named in the header both layers already include, `include/iio_adc_hal.h`:
`enum iio_adc_gain` and `enum iio_adc_reference`. The tables use designated
initializers keyed to those enums, so the correspondence is checked by the
compiler rather than by a comment. `iio_adc_hal.h` is the equivalent of Zephyr's
shared `adc.h` here.

### Attribute plumbing

- values are formatted by `adc_emit_int` / `adc_emit_str` / `adc_emit_micro`
  (`:66-100`) and parsed by `adc_parse_int` / `adc_parse_micro` (`:102-169`):
  `scale` is `int.micro`, `gain`/`reference` are strings from the tables, the
  rest are integers. Every emitter checks `snprintf` truncation and returns
  `-EINVAL` rather than a cut value.
- `adc_channel_index` (`:188-200`) resolves a channel id to an index by `strcmp`
  against `iio_adc_hal.channels[]`.
- `iio_adc_read_attr` (`:377-447`) — `raw` and `process` call
  `iio_adc_hal.read_raw()`; `process` scales by the user-written
  `chan_state[].scale_val*`, so it does **not** depend on `ref_voltage_mv`.
- `iio_adc_write_attr` (`:449-542`) — `raw` and `process` are read-only,
  `-EPERM`. `gain` and `reference` push to hardware through the HAL hook before
  storing (§8).
- `adc_channel_format` (`:222-236`) computes each channel's format **exactly as
  Zephyr's `io_channels.c` does**:

  ```c
  .length     = NO_OS_DIV_ROUND_UP(res, 8) * 8,
  .bits       = res,
  .is_signed  = differential,
  .with_scale = true,
  .scale      = vref_mv / (1 << (res - is_signed)),   /* one LSB in mV */
  .is_be      = true,
  ```

  with `res = iio_adc_hal.resolution_bits` (validated 1..32 in `iio_adc_init`).
  The 12-bit MAX32690 therefore publishes `be:u12/16`, the 16-bit demo
  `be:u16/16`. The format is part of the context XML, so it reflects the
  `differential` setting **at context creation**; flipping `differential`
  later changes the attribute but not the published format (Zephyr has the same
  property).
- `adc_regs[16]` (`:544-564`) backs `REG_READ`/`REG_WRITE` with plain RAM. It is a
  scratchpad for protocol testing, not real register access.

### The sample path

`iio_adc_read_samples` (`:250-332`) is the no-OS rendering of Zephyr's
`iio_device_io_channels_readbuf`:

1. `sample_size = iio_device_get_sample_size(iio_dev, mask)`; `bytes` must be a
   whole number of scans, else `-EINVAL`
2. walk the channels in order, skipping any that is not a scan element or not
   enabled in `mask`; for each kept channel record its HAL index, bits, storage
   bytes and **offset, aligned to its own size** — the same layout
   `iio_device_get_sample_size` describes, so host and board agree
3. per scan: `iio_trigger_timer_wait()`, then one `read_raw` per enabled
   channel, masked to `bits` (two's complement is kept for signed channels) and
   stored big-endian at its offset

Enabling only `voltage1` on the demo gives a 2-byte scan of channel 1 alone;
enabling both gives `v0 v1 v0 v1 …`. `iio_adc_enable_buffer` (`:334-341`) stops
the trigger on disable; the next stream's first wait restarts it.

What it still does not do: per-channel chunked capture through the sequencer
(Zephyr's `extra_samplings`) — each value is one `read_raw`, which is what makes
the timer pacing meaningful (§7a).

## 7a. `drivers/iio_trigger_timer.c` — the sampling trigger

Why it exists: without it `read_samples` converts back-to-back, as fast as the
CPU allows, so the interval between samples depends on the ADC, the bus and the
number of enabled channels — the data is fast but its time axis is meaningless.
The trigger makes each scan start on a hardware-timer tick, so samples are evenly
spaced at a known rate.

Device, as the host sees it:

```
trigger0: timer0
	1 device-specific attributes found:
		attr  0: sampling_frequency value: 1000
```

and `iio-adc` reports `trigger0` as its trigger (`iio_info` shows it; over the
wire the responder's `GETTRIG` maps `noos_get_trigger`'s result to a device
index).

Mechanics (`iio_trigger_timer.c`):

- `iio_trigger_timer_init` — called from `iio_adc_init` — sets up the IRQ
  controller and fills in the `NO_OS_EVT_TIM_ELAPSED` callback. The ISR only
  does `ticks++`.
- `iio_trigger_timer_wait` — returns at once when `sampling_frequency` is 0
  (free running, the old behaviour). Otherwise starts the timer on first use
  (`ticks_count = TRIGGER_TIMER_FREQ_HZ / freq_hz`), spins until `ticks`
  changes, then sets `seen = ticks`. Start order is `no_os_timer_init` →
  `no_os_irq_register_callback` → `no_os_irq_enable` → `no_os_timer_start`,
  as `iiod/usb.c` does for its tick: on Maxim, registering the callback is what
  sets the timer's interrupt enable, and `no_os_timer_init` resets the timer
  (`MXC_TMR_Shutdown` + `MXC_TMR_Init`). Registering first leaves the timer
  running without interrupts and the wait spins forever on the first buffer. A scan slower than the period **resyncs**
  rather than bursting to catch up, so spacing never collapses.
- The callback is registered and unregistered **with interrupts off**
  (`trigger_cb_register`/`_unregister`). See the §13 gotcha on the shared
  callback-list iterator: without this, a stop racing the USB tick could free
  the tick's callback and freeze the board in a TMR0 interrupt storm.
- `iio_trigger_timer_stop` — stop, disable IRQ, remove timer. Called on buffer
  disable and on every `sampling_frequency` write, so the new rate applies
  from the next wait.
- `sampling_frequency` — Hz, default 1000, range 0..100000
  (`IIO_TRIGGER_TIMER_{DEFAULT,MAX}_HZ`).

Deliberate differences from Zephyr's `trigger_timer.c`:

- **`sampling_frequency` in Hz, one scan per tick**, vs Zephyr's
  `sampling_period` in ms filling one whole *block* per tick. Per-scan pacing is
  what gives evenly spaced samples; per-block pacing only spaces the blocks.
  `sampling_frequency` is also the attribute name of the Linux
  `iio-trig-hrtimer`, so host tooling already knows it.
- **Spin, not work queue.** `NO_THREADS=1` (§10): the wait blocks the
  interpreter for `nb_samples / sampling_frequency` seconds. At 1 kHz a
  100 000-sample block takes 100 s, longer than a client's default timeout —
  raise the rate, use smaller blocks, or write 0.
- **Fixed pairing.** `info->trigger` names the trigger at build time.
  `set_trigger` accepts that trigger and refuses any other, so
  `iio_rwdev -t timer0 -r <Hz>` works: `-r` is a `sampling_frequency` write.

Why no-OS's own `iio/iio_trigger.c` is not reused: `iio_hw_trig`/`iio_sw_trig`
are part of no-OS's *own* IIO server — they take a `struct iio_desc`, and fire
through `iio_process_trigger_type`/`iio_trigger_notify` into that server's
buffer machinery. This port uses libiio's server (`iiod/responder.c`) instead, so
there is no `iio_desc` to hand them. What is reused is the pattern — a
`no_os_timer` + `no_os_irq` callback, as in the ad7091r8-sdz
`iio_timer_trigger_example` — through the same no-OS HAL APIs.

Board wiring lives in `platform/maxim/timers.h`, included by `parameters.h`
and by the trigger driver alone (so the driver does not pull in the static UART
and network init structs):

| define | value | used by |
|---|---|---|
| `TRIGGER_TIMER_ID` / `_FREQ_HZ` | 1 / 1 MHz | trigger |
| `TRIGGER_IRQ_ID` / `_HANDLE` / `_PRIORITY` | `TMR1_IRQn` / `MXC_TMR1` / 3 | trigger |
| `USB_TICK_*` | TMR0, priority 3 | USB transport only (`#ifdef NO_OS_USB_TRANSPORT`) |

The Maxim IRQ controller is a singleton, so the trigger and the USB tick share
it and must use different timers. They also share one priority, so neither timer ISR
preempts the other while it walks the callback list.

## 8. `struct iio_adc_hal` and the part drivers

`include/iio_adc_hal.h`:

```c
struct iio_adc_hal {
	const char *const *channels;
	unsigned int       num_channels;
	unsigned int       resolution_bits;
	int                ref_voltage_mv;
	int (*init)(void);
	int (*read_raw)(unsigned int channel, int *value);
	int (*set_gain)(unsigned int channel, unsigned int gain);
	int (*set_reference)(unsigned int channel, unsigned int reference);
};

extern const struct iio_adc_hal iio_adc_hal;
```

**Link-time switch.** The symbol is declared once and *defined* once per firmware
image; CMake compiles exactly the one selected driver
(`CMakeLists.txt:140-144`, driven by the `ADC` cache variable) and the linker
resolves it. Consequence: one ADC per image, chosen at build time. No runtime
registry, no indirection cost.

`set_gain` and `set_reference` are **optional**. `iio_adc_write_attr` calls the
hook only when it is non-NULL, and stores the new value only when the hook returns
0 — hardware first, state second. This is Zephyr's order:
`io_channels.c:442-468` builds a `struct adc_channel_cfg`, calls
`adc_channel_setup`, and assigns `data->overrides[index].adc.X` only on success.
A NULL hook means the value is accepted and stored as before, so drivers that do
not implement it are unaffected.

A hook may reject values the silicon cannot produce. The attribute stays fully
visible and enumerable — interface parity with Zephyr — and the write fails
honestly with `-ENOTSUP` instead of being silently accepted.

`read_raw` has to be a function pointer because the MSDK ADC API genuinely differs
per part, despite both parts including a header called `adc.h`:

- **max32690** (`drivers/adc/max32690/max32690.c`) — slot sequencer plus FIFO.
  `MXC_ADC_Init(&cfg)` takes a config struct. Each read does
  `MXC_ADC_Clear_ChannelSelect` → `MXC_ADC_SlotConfiguration` →
  `MXC_ADC_Configuration` → `MXC_ADC_StartConversion()` → spin on
  `MXC_F_ADC_INTFL_SEQ_DONE` with a timeout → `MXC_ADC_DisableConversion` →
  `MXC_ADC_GetData` (`drivers/adc/common_api/common_api.c`).
  `MXC_ADC_GetData(int *)` copies **every** word in the FIFO, and back-to-back
  conversions leave more than one there: it gets a `MAX_ADC_FIFO_LEN` buffer
  and the first word is kept. Handing it a single `int` overwrote the caller's
  stack — `iio_adc_read_samples`' channel table — and the second sample of
  every stream failed with `-EINVAL`.
  12-bit, 1250 mV internal reference. Implements `set_reference` via
  `MXC_ADC_ReferenceSelect`: `IIO_ADC_REF_INTERNAL` → `MXC_ADC_REF_INT_1V25`,
  `IIO_ADC_REF_EXTERNAL0` → `MXC_ADC_REF_EXT`, everything else `-ENOTSUP`.
  `MXC_ADC_REF_INT_2V048` exists in silicon but has no name in the shared
  enumeration, so it is currently unreachable — an honest gap, not a reason to
  overload `External1`. No PGA, so `set_gain` stays NULL.
- **max32655** (`drivers/adc/max32655/max32655.c`) — `MXC_ADC_Init()` takes no
  argument and `MXC_ADC_StartConversion(channel)` *returns the sample*. 10-bit,
  1220 mV.
- **adc_demo** (`drivers/adc/adc_demo/adc_demo.c`) — `voltage0` + `voltage1`,
  16-bit, walking a 32-entry `sine_lut` with a per-channel `lut_index` (`:13`,
  `:25`, `:42-43`). Needs no hardware, and being the only two-channel driver it is
  the build where the channel-mask interleave in §7 is directly observable.

**Ordering trap on the 690.** `MXC_ADC_Configuration()` calls
`MXC_ADC_SlotsConfig(req)` and then `MXC_ADC_Clear_ChannelSelect()`
(`MaximSDK/.../Source/ADC/adc_me18.c:295-311`). Per-slot
`MXC_ADC_ChSelectConfig()` calls must therefore come **after**
`MXC_ADC_Configuration`, never before. The current one-shot code survives only
because it redoes slot configuration on every read. This is the most likely silent
failure in any future multi-slot sequencer work.

## 9. Known gaps

Each with the Zephyr code that already solves it, so picking one up is a read
rather than a re-derivation. Details in §14.

| gap | where | solved in RaluZephyr by |
|---|---|---|
| synchronous enqueue; `nonblock` ignored | `backend.c:153-184` | pending list + `ready_sem`, `backend.c:374-416`, `trigger_timer.c:151-186` |
| trigger pairing fixed at build time; `set_trigger` only accepts the fixed one | `backend.c:225-257` | `zephyr_set_trigger`, `include/iio_trigger.h` |
| `cancel_buffer` cannot abort | `backend.c:81-83` | `zephyr_cancel_buffer:222-225` unsubscribes and wakes blocks |

Closed since the upstream reset (kept here so nobody re-opens them): channel
mask stored and passed to `read_samples` (§5, §7); mask-filtered interleave of
enabled channels (§7); per-channel computed format (§7); a real trigger device
(§7a); `IIO_ADC_MAX_CHANNELS` checked against `num_channels` (§7).

No Zephyr analogue, specific to this port:

- `iio_adc_hal` is a single link-time symbol and `chan_state[]` is file-scope, so
  exactly one ADC instance is possible per image
- `adc_regs[]` is RAM, not registers (§7)
- upstream `iiod/responder.c` prints `int32_t` with `%u`, which warns under
  arm-none-eabi; left alone because files outside `no-OS/` track upstream
  exactly
- serial has one client at a time and no disconnect: two contexts on the tty
  (`iio_stresstest -t 2`) interleave on the wire, and a client killed in the
  middle of a block leaves the board sending it to nobody, so the next client
  starts out of sync. Both need a board reset; exit clients with Ctrl-C
- latent NULL-deref in `iiod/responder.c:1249-1260` — `iiod_cmd` bounds-checks
  `cmd->op` against `IIOD_NB_OPCODES` but `iiod_op_functions[]` is a sparse
  designated-initializer array, so an unimplemented-but-in-range opcode calls a
  NULL pointer

## 10. Concurrency model

`NO_THREADS=1` (`CMakeLists.txt:106`). Two consequences that shape any async
design:

- `iio_task_token_do_enqueue` runs `iio_task_process` **inline on the calling
  thread** (`task.c:215-216`). There is no worker.
- `iiod/responder.c:330` always calls `iio_block_dequeue(entry->block, false)` —
  blocking.

So making capture interrupt-driven buys "the CPU sits in WFI while the hardware
sequences at its own rate" and **not** overlap between capture and protocol work.
That is still worth having, but it is not throughput.

The **client** (the PC) is a normal threaded libiio build; none of this applies
to it. Only the board is single-threaded, and it serves one command at a time:
while a block is being filled at the trigger rate, the board answers nothing
else.

If you add a WFI wait, guard the completion test with `__disable_irq()` around it
or the wakeup can arrive between test and sleep and be lost.

Also verified, and blocking for interrupt-driven ADC on the MAX32690:

1. **No ADC interrupt reaches no-OS.** `maxim_irq.c` defines ISRs for UART0-3,
   TMR0-2, DMA0-15, RTC and USB — there is no `ADC_IRQHandler` — and
   `enum no_os_irq_event` has no ADC event. Registering a callback for `ADC_IRQn`
   returns success and never fires. The port would have to define its own
   `ADC_IRQHandler`; the startup file's version is `.weak`, so that works.
2. **MSDK's async callback carries no instance pointer.** `adc_revb.c:49` is a
   single file-scope `static mxc_adc_complete_cb_t async_callback;` and `:300`
   invokes `(cb)(NULL, flags)`. `MXC_ADC_StartConversionAsync` also spins in
   `MXC_GetLock`. Write the ISR directly instead.
3. **ADC-DMA is unreachable.** `MXC_ADC_StartConversionDMA` needs
   `MXC_DMA_Handler` to run from `DMAn_IRQHandler`, but `maxim_irq.c:210-285`
   provides *strong* `DMA0..15_IRQHandler` that dispatch through no-OS's own
   action list and never call it.

## 11. Transports

One transport per image, selected by `WITH_IIOD_{UART,NETWORK,USB}`. Device
registration is done once, in `samples/iiod/main.c:noos_register_devices`, before
`noos_iiod_run()`; each transport then has the same shape:

```
iiod_init()
iio_create_context(&params, "no-os:")
iio_context_get_xml(ctx)           -> the context description sent to the host
loop { wait for a client; iiod_interpreter(ctx, pdata, read, write, xml, len) }
```

**Everything outside `no-OS/` is upstream `analogdevicesinc/libiio` main,
unmodified** — `tinyiiod/`, `iiod/responder.c`, `iiod-responder.c`. Earlier
work patched tinyiiod and the responder (a non-blocking step API, the v0 ASCII
protocol, multiple network clients); that is on branch
`backup/pre-upstream-reset-20260928`. The transports were rewritten to the
upstream contract instead:

- `iiod_interpreter()` **blocks** for a whole session and returns when a read
  or write fails. It handles the `"BINARY\r\n"` switch itself; only the v1
  binary protocol is served (`WITH_IIOD_V0_COMPAT=0`; `tinyiiod/ops-stubs.c`
  supplies the one v0 hook the responder links against).
- the read callback must **block until it has all `size` bytes**. Returning
  `-EAGAIN` ends the session, so no transport returns it; an idle or dead link
  is reported as `-ETIMEDOUT` / `-ENODEV`, which ends the session cleanly and
  the loop waits for the next client.

Transport specifics:

- **UART** (`iiod/uart.c`) — the read loops over `no_os_uart_read`, retrying on
  `-EAGAIN`. The interpreter never returns while the UART exists, so there is
  no session boundary.
- **Network** (`iiod/network.c`) — lwIP plus a netdev chip
  (ADIN1110 via no-OS Kconfig, `configs/network.conf`), static IP by default,
  port 30431. **One client at a time**: the accept loop steps lwIP until a
  connection arrives, runs the interpreter on it, then `net_client_close` +
  `net_drain`. The read steps lwIP while waiting and gives up after
  `NET_READ_TIMEOUT_S` (3 s) idle. Consequence: a client that opens a second
  socket (libiio's network backend does, for a buffer stream — `iio_rwdev`)
  has that socket accepted only after the first one idles out, i.e. about 3 s
  of latency before streaming starts. It works; it is not fast.
- **USB** (`iiod/usb.c`) — MAXUSB vendor interface with `IIO_USB_NUM_PIPES`
  (2) bulk pipe pairs, fed by a TMR0 tick that keeps an RX transfer queued per
  open pipe. **One interpreter serves both pipes**: a read with no command in
  progress waits on every open pipe and takes the first with data
  (`pipe_wait_any`), then stays locked on that pipe until the response is
  written, so a command's payload and reply never cross pipes. A session is
  "pipe 0 open with the `open_seq` it started with"; the host closing or
  reopening pipe 0, a bus reset or a disconnect ends it (`-ENODEV`), and
  `session_close` drops what every pipe still holds — except a pipe 0 the host
  has already reopened, which is the next session.
  A data pipe the host gives up on (Ctrl-C twice in `iio_rwdev`: the host
  cancels the pipe-1 transfer and sends the block/buffer teardown on pipe 0)
  is **stalled**, not fatal: a response blocked on it for `USB_STALL_TICKS`
  (200 ms) while pipe 0 has a command, or an RX error on it, drops the pipe
  (`pipe_stall`), its later responses fail with `-EPIPE`, and the session goes
  on over pipe 0. Reopening the pipe clears it. Without this the interpreter
  sat out the 10 s TX timeout, then ended the session on the pipe-1 error, and
  the host waited forever for its pipe-0 replies.

`iiod/responder.c` (1357 lines) is the shared v1 binary protocol implementation.
Opcode dispatch table at `:1217-1248`, covering attrs, buffers, blocks, event
streams, triggers and register access; `iiod_cmd` (`:1249-1263`) dispatches through
it. `buffer_enqueue_block` (`:293-320`) shifts enqueue errors left by 16 so the
client can tell an enqueue failure from a dequeue failure in one status word.

## 12. Value formatting

There is no shared value-formatting module any more; each device class formats
its own attributes with small `snprintf` helpers that check for truncation
(`adc_emit_*` in `iio_adc.c`, `snake_emit_*` in `samples/iiod/main.c`, the
`sampling_frequency` read in `iio_trigger_timer.c`). The text forms match what a
Linux IIO driver prints: plain integers, `int.micro` with six fractional digits
for `scale`, and bare strings for enumerations. A read returns the length
**including** the terminating NUL, as the responder expects.

Every attribute read is capped at 256 bytes on this build:
`iiod/responder.c:24-26` sets `READ_ATTR_BUF_SIZE 256` under
`WITH_LIBTINYIIOD` (64 KiB otherwise), so keep values short.

## 13. Build system

The firmware is built **by upstream no-OS's own CMake + Kconfig**, the same way a
project under `no-OS/projects/` is. This port carries no toolchain files, board
files or MSDK source lists of its own: the compiler, cpu flags, startup code,
linker script, platform drivers, lwIP, the generated `no_os_config.h` and the
`flash`/`debug` targets all come from the no-OS checkout. `CMakeLists.txt` only
adds the libiio sources and links them against the `no-os` library target.

Nothing in the no-OS checkout is modified; it is only `add_subdirectory`'d.

### Presets

One preset per *transport*, one build dir each (`build-uart`, `build-usb`,
`build-network`, never the repo root). The board is an override, not a preset:

```
cmake --workflow --preset usb                     # configure + build, default board
cmake --build --preset flash-usb                  # flash it (also flash-uart, flash-network)
cmake --workflow --preset flash-usb               # configure + build + flash
cmake --preset usb --fresh -DBOARD=max32690fthr   # another board, same dir
cmake --build --preset usb
```

The default board is `ad-apard32690-sl` (hidden `iiod-base`, which also sets
Ninja, Debug, `compile_commands.json` and `PROBE=openocd`). The image lands in
`build-<transport>/build/libiio-noos.{elf,hex,bin}`.

**The board list is not copied here.** `cmake/no_os_board.cmake` looks `BOARD`
up in no-OS's own `board_configs/*/CMakePresets.json` and takes that preset's
`cacheVariables`, following `inherits`: `PLATFORM`, `TARGET`, `TARGET_NUM`,
`BOARD_CONFIG_FILE` and `USE_VENDOR_TOOLCHAIN`. A board no-OS adds is buildable
here the day the checkout has it. An unknown `BOARD` lists the known ones. A `-D`
for any of those variables still wins over the board's value.

CMake keeps the first configure's compiler and toolchain flags. So a dir that was
configured for one board refuses another, with the `--fresh` command to use,
instead of building the new board with the old one's flags.

Checked on Maxim, all built:

| board | part | uart | usb | network |
|---|---|---|---|---|
| ad-apard32690-sl (default) | max32690 | x | x | x (ADIN1110) |
| max32690fthr | max32690 | x | x | |
| max32650fthr | max32650 | x | x | |
| ad-swiot1l-sl | max32650 | x | x | |
| max32666fthr | max32665 | x | x | |
| max32655fthr | max32655 | x | | |
| max32660fthr | max32660 | x | | |
| max32670evkit | max32670 | x | | |
| max78000fthr | max78000 | x | | |

USB needs the USB-HS core (no-OS has a `maxim_usb_uart.c` for the part);
`platform/maxim/platform.cmake` refuses it on the others with that reason.
Network assumes the ADIN1110 wiring of the AD-APARD32690-SL. Only that board
has been run on hardware.

### Finding no-OS: `cmake/no_os_locate.cmake`

No path is hardcoded. It runs before `project()` (the toolchain file lives in
the checkout) and takes the first hit of:

1. `-DNO_OS_PATH=<dir>`
2. `$NO_OS_PATH`, `$NO_OS_DIR` or `$NOOS_DIR`
3. `<libiio>/../no-OS`, `~/no-OS`, `<libiio>/../*/no-OS`, `~/*/no-OS`
4. `-DNO_OS_FETCH=ON`: shallow clone of `NO_OS_GIT_TAG` (default `main`) into
   `build-<preset>/_deps/no-os`

A tree only counts if it has the CMake build (`board_configs/<PLATFORM>`,
`tools/scripts/generate_config.py`, `drivers/platform/<PLATFORM>/toolchain.cmake`),
so an old Make-only checkout is skipped. Several hits give a warning listing them;
the choice is cached as `NO_OS_PATH`.

### How the bridge works

- `CMAKE_TOOLCHAIN_FILE` is set to `${NO_OS_PATH}/drivers/platform/${PLATFORM}/toolchain.cmake`
  before `project()`; no-OS only picks one itself when it is the top-level project.
- The Kconfig input is the board defconfig from no-OS, then the project fragment.
  The fragment is `configs/<transport>.conf` (generic: UART, IRQ, GPIO, TIMER,
  DMA, plus SPI/NET/LWIP/ADIN1110/static IP for network) followed by
  `platform/<PLATFORM>/configs/<transport>.conf` (e.g. `CONFIG_USB_UART_MAXIM`).
  They are merged into `build-*/iiod/<transport>.conf` and passed as
  `PROJECT_DEFCONFIG`, relative to this directory as no-OS expects.
  `boards/<transport>/<BOARD>.conf`, if one is added, overrides per board.
- `CONFIG_IIO` stays off: tinyiiod is the server, not no-OS's iio app.
- After `add_subdirectory(${NO_OS_PATH} no-os)`, `CMAKE_MODULE_PATH` gets the
  same dirs no-OS's own root `CMakeLists.txt` uses (`cmake`, `cmake/stm32`,
  `cmake/libraries`), plus `cmake/${PLATFORM}` when it exists. `project_utils`
  is included for `config_platform_sdk`, `add_flash_target` and
  `post_build_config`. `config_platform_sdk` is what honours no-OS's
  per-platform layout: it includes `cmake/<platform>/<platform>_platform_sdk.cmake`
  (aducm3029, pico, stm32, xilinx) and calls its `config_<platform>_sdk()`.
  Maxim has none, because its SDK is set up by the toolchain file.
- `platform/<PLATFORM>/platform.cmake` adds only what no-OS's target lists leave
  out (for maxim: the MSDK ADC sources on max32690, which no-OS does not compile
  there; added only if not already in `no-os`'s `SOURCES`, so no duplicate
  symbols) and sets `IIOD_PLATFORM_ADC`: `common_api` on max32690/max32655,
  `adc_demo` (no hardware) everywhere else.

Key variables:

| variable | default | meaning |
|---|---|---|
| `BOARD` | `ad-apard32690-sl` | any no-OS board; everything below it comes from its no-OS preset |
| `PLATFORM`, `BOARD_CONFIG_FILE`, `TARGET`, `TARGET_NUM` | from the board | no-OS platform, defconfig and part |
| `IIOD_TRANSPORT` | `uart` | `uart`, `usb` or `network` |
| `NO_OS_PATH` | discovered | the no-OS checkout |
| `NO_OS_FETCH`, `NO_OS_GIT_URL`, `NO_OS_GIT_TAG` | `OFF`, upstream, `main` | clone fallback |
| `ADC` | `IIOD_PLATFORM_ADC` | `drivers/adc/<adc>/<adc>.c` |
| `PROBE` | `openocd` (preset) | no-OS flash tool; `jlink` also works |
| `MAXIM_LIBRARIES` (env) | from no-OS's maxim toolchain | MSDK `Libraries/` dir |

### Other platforms

The build is not Maxim-only: the board picks the no-OS platform. A platform
without `platform/<PLATFORM>/parameters.h` fails at configure time with a message
that the iiod glue is missing (UART id, trigger timer, IRQ controller: see
`platform/maxim/`). Adding one takes that glue and nothing else: the board already
resolves (`-DBOARD=nucleo-f756zg` gets `PLATFORM=stm32` from no-OS and stops
only at the missing glue). The no-OS side and its SDK install are unchanged.

Without `BOARD` the directory is a host project that builds only the PC-side
API tests (`tests/`).

`COMMON_DEFS` in `CMakeLists.txt` turn off every upstream backend
(`WITH_LOCAL_BACKEND=0` and friends). They also set `WITH_LIBTINYIIOD=1`,
`WITH_EXTERNAL_BACKEND=1` and `NO_THREADS=1`, and pin the version strings.

Gotchas:

- the MSDK startup file sizes the main stack from `__STACK_SIZE`, default
  4 KiB, which the responder overflows as soon as a buffer is enabled.
  `platform/maxim/platform.cmake` defines it on the `no-os` target (where the
  startup file is compiled) from `IIOD_STACK_SIZE`, default 64 KiB — as no-OS
  does itself for aducm3029.
- no-OS's Maxim include path uses the MSDK's old CMSIS 4 `core_cm4.h`, which has
  no `NVIC_GetEnableIRQ`. `iio_usb_backend.c` reads `NVIC->ISER` instead.
- `CONFIG_ADIN1110` is inside `menuconfig NET`, so `CONFIG_NET=y` is required.
- `post_build_config` writes `.vscode/` launch configs into this directory.
- under WSL2, `usbip` must be re-attached after every flash.
- no-OS `maxim_irq.c` keeps every callback in one list with a **single shared
  iterator** (`no_os_list_read_find`), which each timer ISR walks. Two things
  follow:
  - A register or unregister done in thread context and interrupted by that
    walk acts on whatever node the ISR left the iterator on.
  - `_timer_common_callback` returns without `MXC_TMR_ClearFlags` when it finds
    no callback, so the timer's interrupt then fires forever.

  Change the list with interrupts off, and give all timer ISRs the same
  priority. Also, `no_os_irq_ctrl_remove` frees the callbacks of **every**
  peripheral, so never call it on the shared controller.

## 14. Relationship to the Zephyr port

> **Grep `~/RaluZephyr/zephyr/`, not `~/libiio/zephyr/`.**

The in-repo `zephyr/` is a stale snapshot: 104-line `backend.c`, a 4-op
`zephyr_ops` at `:91-96` (`create`, `read_attr`, `write_attr`, `get_trigger`, the
last returning NULL at `:45-49`), no buffer or block ops — so `iio_buffer_open()`
against it would fail `-ENOSYS` (`buffer.c:131-132`).

The live tree (`~/RaluZephyr/zephyr/backend.c`, 442 lines) has a **14-op**
`zephyr_ops` (`:418-434`): `create`, `read_attr`, `write_attr`, `get_trigger`,
`set_trigger`, `open_buffer`, `close_buffer`, `enable_buffer`, `cancel_buffer`,
`readbuf`, `create_block`, `free_block`, `enqueue_block`, `dequeue_block`
(`writebuf` commented out). Patterns 1, 2 and the format are now matched;
pattern 3 (async enqueue) is not, and pattern 4 is matched in spirit with the
differences listed in §7a.

### The four patterns to match

**1. The mask is stored.** *(matched: `backend.c:noos_open_buffer`)* `zephyr_open_buffer:149-173` keeps it in the buffer
pdata (`include/iio_device.h:15-30`):

```c
pdata->zephyr_dev = zephyr_dev;
pdata->iio_dev = dev;
pdata->mask = mask;
```

and every capture path takes it as an argument:
`api->readbuf(zephyr_dev, iio_dev, pdata->mask, dst, len)` (`backend.c:249-336`).
Legitimate because the mask is a `buf_stream`-owned copy with buffer lifetime (§6).
The no-OS equivalent stores it and passes it through the `mask` parameter of
`noos_iio_read_samples_t` (§4).

**2. Capture honours the mask and interleaves.** *(matched:
`iio_adc_read_samples`, §7, minus the chunked sequencer capture)*
`iio_device_io_channels_readbuf` (`io_channels.c:1035`+):

- build `ch_info[READBUF_MAX_ENABLED_CHANNELS]` (8), skipping any channel where
  `!iio_channel_is_scan_element(chn) || !iio_channel_is_enabled(chn, mask)`
- accumulate a per-channel `byte_offset` and a total `bytes_per_sample`
- per channel, capture in chunks of `READBUF_CHUNK_SAMPLES` (256) using
  `adc_sequence_options{.extra_samplings = chunk - 1}`
- write each sample to `out + s * bytes_per_sample + ci->byte_offset`, honouring
  `fmt->shift` and `fmt->is_be`
- return `total_samples * bytes_per_sample`

**3. Enqueue is genuinely asynchronous.** *(not matched — §5, §10)* `zephyr_enqueue_block:374-397` drains
the block's semaphore, appends to `buf->pending_blocks` under a mutex, and submits
the trigger's work item — then returns. `zephyr_dequeue_block:399-416` waits on
`blk->ready_sem`: `K_NO_WAIT` → `-EBUSY` when `nonblock`, else `K_FOREVER`, and
`-EBADF` if the buffer was disabled underneath it. The fill happens in the work
handler (`trigger_timer.c:151-186`): pop a pending block,
`to_write = MIN(blk->bytes_used, blk->size)`, `ops->readbuf(...)`,
`k_sem_give(&blk->ready_sem)`.

Not copyable verbatim: this port has `lock-dummy.c` and `NO_THREADS=1`, so the
mutex/semaphore handshake has to become ISR flags plus a guarded WFI (§10).

**4. Triggers are a device class.** *(matched as a registered device with
`is_trigger`, §7a)* `include/iio_trigger.h`:

```c
struct iio_trigger_ops {
	struct iio_device *(*create)(struct iio_context *ctx, const struct device *dev,
				    enum iio_device_trigger_type type, const char *id);
	void (*init)(const struct device *dev);
	int  (*subscribe)(sys_snode_t *node);
	void (*unsubscribe)(sys_snode_t *node);
};
```

A trigger device is recognised by having **no `add_channels`** — `create_context`
skips those when building the device list (`backend.c:107-110`).
`trigger_timer.c` is the reference instance: a `k_timer` ISR submits to a
dedicated work queue (`:31-35` — deliberately not the system queue, so RTIO
processing is not blocked); `subscribe:92-122` appends the buffer, sets
`buf->trig`, and starts the timer only when `refcnt` reaches 1;
`unsubscribe:124-143` stops it at zero; `sampling_period` is a millisecond device
attribute (`:188-256`). `enable_buffer:180-220` refuses to enable without a
trigger (`-ENODEV`) or with a non-trigger (`-EINVAL`).

This port uses the libiio core side directly: `iio_device_is_trigger`
(`device.c:319`) recognises `trigger0` by its id prefix and zero channels, and
`noos_get_trigger` resolves `info->trigger` with `iio_context_find_device`.
`iio_device_set_trigger` (`:337`) reaches `noos_set_trigger`, which accepts
only the fixed trigger; Zephyr has no `set_trigger` op at all.

### Differences that should stay differences

- **Device discovery.** Zephyr iterates a ROM section,
  `STRUCT_SECTION_FOREACH(iio_device_info, ...)`, populated by
  `IIO_DEVICE_DT_INST_DEFINE` from devicetree. no-OS has no devicetree, hence the
  explicit `noos_iio_register_device()` table (§4).
- **Channel index passing.** Zephyr stores the index in the channel pdata and
  recovers it with `(int)(intptr_t)iio_channel_get_pdata(chn)`; no-OS resolves by
  `strcmp` on the channel id (`adc_channel_index`, `iio_adc.c:92-104`).
- **Locking.** Zephyr links a real `lock-zephyr.c`; no-OS links `lock-dummy.c`
  (`CMakeLists.txt:23`).

### What must stay identical

The **attribute set** — that is the whole point of keeping the ports aligned, and
the reason the tables are 21 and 7 entries wide (§7). RaluZephyr still applies
gain, reference and differential by rebuilding a `struct adc_channel_cfg`, calling
`adc_channel_setup`, and storing the override only on success
(`io_channels.c:442-468`, `:565-591`, `:607-632`) — the order this port's HAL
hooks now follow (§8).

Two further points where the ports used to diverge, now aligned:

- her scan elements are `true` (this port matches)
- her per-channel format is *computed*: `.length = ceil(resolution / 8) * 8`,
  `.bits = resolution`, `.is_signed = differential`, `.with_scale = true`,
  `.scale = vref_mv / (1 << (resolution - is_signed))`, `.is_be = true` — this
  port now computes the same (`adc_channel_format`, §7)
