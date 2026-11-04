# libiio on CAPI

`capi/` is libiio's port to [CAPI](https://github.com/adi-innersource/core-sw-common-hal):
it serves devices behind CAPI interfaces to libiio clients (`iio_info`,
`iio_attr`, `iio_rwdev`) over a `capi_uart`. A device driver only implements
its CAPI interface (`capi_adc_ops`, `capi_accel_ops`, `capi_temp_ops` ...); a
class adapter (`iio_capi_adc`, `iio_capi_accel`, `iio_capi_temp`) turns it
into an IIO device, with no per-driver code.

```
app main.c ──► driver (capi_adc_ops) ──► capi_adc
           ──► libiio/capi: iio_capi_adc ─► backend_table ─► backend ─► iiod/uart ──► capi_uart ──► host
```

The worked example serves the MCU's own ADC on an AD-APARD32690-SL
(MAX32690, `max32690_adc` driver from capi-noos) or a MAX32655FTHR
(MAX32655, `max32655_adc`), from two places: a capi-hello-world application
that builds for both boards, and a capi-noos project per board
(`projects/max32690-adc-iio`, `projects/max32655-adc-iio`).

## Repositories

Clone each on the branch shown. capi-noos is private: its members can open it, others ask Aurel Miron for access.

| Repository | Branch | What it brings |
|---|---|---|
| [Ramona-Gradinariu_adi/core-sw-common-hal](https://github.com/Ramona-Gradinariu_adi/core-sw-common-hal/tree/feature/LIBIIOCHAL-5-libiio-support) (fork of adi-innersource/core-sw-common-hal) | `feature/LIBIIOCHAL-5-libiio-support` | CAPI: interfaces, dispatch, SDK cmake (`CAPI_DIR`); this branch adds `capi_value`, `capi_accel`, `capi_temp` and the `capi_adc` the drivers here use, until it is merged into adi-innersource/core-sw-common-hal |
| [adi-innersource/capi-maxim](https://github.com/adi-innersource/capi-maxim/tree/capi-libiio-max32690-adc) | `capi-libiio-max32690-adc` | platform backend; this branch adds interrupt-driven UART receive, which buffer captures need |
| [Aurel-Miron_adi/capi-noos](https://github.com/Aurel-Miron_adi/capi-noos/tree/capi-libiio-max32690-adc) (from `develop`) | `capi-libiio-max32690-adc` | the `max32690_adc` and `max32655_adc` drivers, and the `max32690-adc-iio` and `max32655-adc-iio` projects |
| [CioRadu1/libiio](https://github.com/CioRadu1/libiio/tree/capi-libiio-v2) (fork of analogdevicesinc/libiio) | `capi-libiio-v2` | `capi/`, this port; also the host tools |
| [adi-innersource/capi-hello-world](https://github.com/adi-innersource/capi-hello-world/tree/capi-libiio-max32690-adc) | `capi-libiio-max32690-adc` | the hello-world application |

Also needed: the [MaximSDK](https://www.analog.com/en/resources/evaluation-hardware-and-software/embedded-development-software/software-development-kit-sdk-maxim.html)
(its arm-none-eabi-gcc 10.3 and OpenOCD are used), CMake >= 3.27, Ninja,
Python 3, and for the host tools `libserialport-dev`, `libxml2-dev`,
`libzstd-dev`, `libusb-1.0-0-dev`. Board: AD-APARD32690-SL or MAX32655FTHR,
through its on-board debugger's USB (one cable: flashing and the UART).

## 1. Workspace

Everything in one directory, `$CAPI_WORKSPACE`; each `module.yml` finds its
dependencies there.

```bash
export CAPI_WORKSPACE=~/capi-ws
mkdir -p $CAPI_WORKSPACE && cd $CAPI_WORKSPACE

git clone -b feature/LIBIIOCHAL-5-libiio-support https://github.com/Ramona-Gradinariu_adi/core-sw-common-hal
git clone -b capi-libiio-max32690-adc https://github.com/adi-innersource/capi-maxim
git clone -b capi-libiio-max32690-adc https://github.com/Aurel-Miron_adi/capi-noos
git clone -b capi-libiio-v2 https://github.com/CioRadu1/libiio
git clone -b capi-libiio-max32690-adc https://github.com/adi-innersource/capi-hello-world

export CAPI_DIR=$CAPI_WORKSPACE/core-sw-common-hal
export MAXIM_SDK_PATH=~/MaximSDK          # where the MaximSDK is installed
export PATH=$MAXIM_SDK_PATH/Tools/GNUTools/10.3/bin:$MAXIM_SDK_PATH/Tools/OpenOCD:$PATH
```

These `export`s are needed in every new shell.

## 2. Host tools

Any libiio v1 client works (`iio_info -V` says `version: 1.0`). Without one,
build and install it from the same branch:

```bash
cd $CAPI_WORKSPACE/libiio
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWITH_SERIAL_BACKEND=ON -DWITH_IIOD=OFF
cmake --build build -j8
sudo cmake --install build && sudo ldconfig
```

The configure summary must list `serial` under "Features enabled".

## 3. Connect the board

On Linux it shows up as `/dev/ttyACM0`. Under WSL, attach it from Windows
first, with [usbipd-win](https://github.com/dorssel/usbipd-win), once per plug-in
(the debugger is `0d28:0204`):

```bash
"/mnt/c/Program Files/usbipd-win/usbipd.exe" list
"/mnt/c/Program Files/usbipd-win/usbipd.exe" attach --wsl --busid <BUSID>
ls /dev/ttyACM*
```

## 4. Build and flash

The hello-world application:

```bash
cd $CAPI_WORKSPACE/capi-hello-world
cmake --preset apard32690          # or: cmake --preset max32655fthr
cmake --build build-uart
cmake --build build-uart --target flash
```

or the capi-noos project, with the same three commands in
`$CAPI_WORKSPACE/capi-noos/projects/max32690-adc-iio` (preset `apard32690`)
or `projects/max32655-adc-iio` (preset `max32655fthr`).

Both hello-world presets build in `build-uart`: to switch boards, configure
with `--fresh`. `Verified OK` ends a good flash. If `/dev/ttyACM0` is gone afterwards
(WSL), run the `attach` again.

## 5. Test

```bash
U=serial:/dev/ttyACM0,115200
DEV=max32690-adc                   # on the MAX32655FTHR: DEV=max32655-adc

iio_info -u $U
iio_attr -u $U -c $DEV voltage0 raw
iio_attr -u $U -c $DEV voltage0 scale
iio_attr -u $U -c $DEV voltage0 raw 5
iio_rwdev -u $U -b 256 -s 4096 $DEV voltage0 voltage1 > cap.bin
ls -l cap.bin
python3 -c "import struct;b=open('cap.bin','rb').read();v=struct.unpack('<%dH'%(len(b)//2),b);print(len(v),'samples, min',min(v),'max',max(v))"
iio_reg -u $U $DEV 0
```

What a working board gives:

| Command | AD-APARD32690-SL | MAX32655FTHR |
|---|---|---|
| `iio_info` | device `max32690-adc` (buffer capable): `voltage0`..`voltage3`, `format: le:u12/16>>0`, each with `raw` and `scale`; `buffer0: (input)` with 4 scan elements | device `max32655-adc`, the same with `format: le:u10/16>>0` |
| `iio_attr ... raw` | a code 0..4095 | a code 0..1023 |
| `iio_attr ... scale` | `0.305175781` (1250 mV / 2^12) | `1.191406250` (1220 mV / 2^10) |
| `iio_attr ... raw 5` | refused: `Operation not permitted` | same |
| `iio_rwdev` | `cap.bin` of 16384 bytes (4096 scans x 2 channels x 2 bytes) | same, every value <= 1023 |
| `iio_reg` | refused: `No such file or directory` (the driver has no registers) | same |

`raw x scale` is the input in mV. To check a real voltage, put 0 V up to the
reference on AIN0: 0.625 V reads about 2048 on the MAX32690, about 524 on the
MAX32655.

An accelerometer works the same way: capi-hello-world (preset
`max32655fthr`) serves an EVAL-ADXL355-PMDZ through
capi-noos's `adxl355` driver (`capi_accel_ops`) and `iio_capi_accel`, as the
device `adxl355` with `accel_x`/`accel_y`/`accel_z` (`raw`, `scale` in m/s²,
writable from its `scale_available` list to choose the range,
`calibbias`, `sampling_frequency`, `filter_high_pass_3db_frequency`, and
`_available` lists for the writable
ones) and `temp` (`raw`, `scale`, `offset`: (raw + offset)
x scale in m°C; in scans after the axes), with the scan formats
(`be:s20/32>>4`; temp `be:u12/16>>0`, unsigned as the datasheet has
it), the driver's temperature sensor
(`capi_temp_ops`) given to `iio_capi_accel` with its accelerometer. Its buffer is the part's
own samples, paced by `sampling_frequency`: a driver that streams
(`capi_accel` `stream_enable`/`read_samples`) gives a block's scans through
the table's `read_scans`, a block at a time; see capi-hello-world's README
for how continuous a capture is over the UART.

## Using it in your own application

```cmake
add_subdirectory("${LIBIIO_DIR}/capi" "${CMAKE_BINARY_DIR}/libiio")
target_link_libraries(<app> PRIVATE libiio::capi)
```

```c
/* after capi_uart_init(), capi_adc_init() and capi_adc_chan_init() */
/* the channels' layout, scale ... are as the driver describes them */
iio_capi_adc_register(&iio_adc, "my-adc", chan_handles, nb_chans);
/* after capi_accel_init() and capi_temp_init(), temp_handle may be NULL */
iio_capi_accel_register(&iio_accel, "my-accel", accel_handle, temp_handle);
/* temperature sensors of their own, a capi_temp_init() handle each: channels
 * temp, temp<n>, temp_ambient, temp<n>_object ... as each handle's kind and
 * number say, a buffer of their raw codes, and
 * thermocouple_type and oversampling_ratio (with its list) when the driver
 * has them */
struct capi_temp_handle *temps[] = { ambient_handle, object_handle };
iio_capi_temp_register(&iio_temp, "my-temp", temps, 2);
return iiod_uart_run(uart, NULL);   /* the app's UARTn_IRQHandler calls capi_uart_isr(uart) */
```

## Troubleshooting

- **`CAPI_DIR not set`**: the `export`s of step 1 are missing in this shell.
- **No `/dev/ttyACM0`**: attach it again (step 3); its bus id can change.
- **A client hangs after another was killed mid-capture** (Ctrl-C on a
  running `iio_rwdev`): the debugger's USB-UART bridge, not the firmware,
  stops passing data; unplug and plug the board back in.
- **A capture sometimes stops early with `Connection timed out` (WSL)**: WSL2
  steps the PC's wall clock back now and then (about 0.76 s every ~30 s), and
  libiio's host side times its waits with that clock. Not the board: run it
  again, or pass `-T -1` (no timeout) to `iio_rwdev`.
- **Captures stop at the 5th block with "Invalid argument"**: the build used
  a capi-maxim without the interrupt-driven UART receive; check that
  `$CAPI_WORKSPACE/capi-maxim` is on `capi-libiio-max32690-adc`.

## Directory structure

```
capi/
├── CMakeLists.txt          # libiio's core + this port, as libiio::capi
├── iiod/uart.c             # transport: capi_uart, interrupt-driven receive, one session per client
├── backend.c               # the "capi:" backend: context, attributes, buffers, registers
├── backend_table.c         # devices described by a channel table: ids, attributes, values, scans (one at a time, or a block's at once)
├── iio_capi_adc.c          # adapter: an ADC behind capi_adc as a table device
├── iio_capi_accel.c        # adapter: an accelerometer behind capi_accel, with its capi_temp (built when CAPI has capi_accel.h)
├── iio_capi_temp.c         # adapter: temperature sensors behind capi_temp, a device (with a buffer) or another's channel (built when CAPI has capi_temp.h)
└── include/
    ├── iiod_uart.h         # iiod_uart_run()
    ├── iio_device.h        # the device API backend.c serves
    ├── iio_capi_table.h    # the channel-table format
    ├── iio_capi_adc.h      # iio_capi_adc_register()
    ├── iio_capi_accel.h    # iio_capi_accel_register()
    ├── iio_capi_temp.h     # iio_capi_temp_register()
    └── iio-config.h
```

## License

MIT, as the files' SPDX headers say.
