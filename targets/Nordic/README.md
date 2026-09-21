# Nordic Semiconductor nRF Connect SDK (Zephyr) Support

This platform supports hardware that runs the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) (NCS), which is based on the Zephyr RTOS. nanoCLR is built as a Zephyr *module* (see `_nanoCLR_module`) inside a minimal Zephyr application (`nanoCLR/main.c` in each target folder). As Zephyr handles most low level functions, porting to new hardware based on the Nordic nRF9160 or nRF52840 should be quite simple.

Current support is provided for GPIO and I2C. More support is planned for the future.

## Supported targets

| Target | Zephyr board | Notes |
|---|---|---|
| [NRF52840_DK](NRF52840_DK/README.md) | `nrf52840dk/nrf52840` | |
| [NRF9160_DK](NRF9160_DK/README.md) | `nrf9160dk/nrf9160/ns` | non-secure application (TF-M is built and merged in) |

## Versions

- This build has been tested using the nRF Connect SDK **v3.4.1** (Zephyr 4.4) and Zephyr SDK **1.0.1**.
- Both versions are pinned in [`CMakeLists.txt`](CMakeLists.txt) (`NRF_CONNECT_SDK_DEFAULT_VERSION`, `ZEPHYR_SDK_DEFAULT_VERSION`) and can be overridden with the CMake variables `NRF_CONNECT_SDK_VERSION` and `ZEPHYR_SDK_VERSION`.

## Pre-requisites

- CMake and Ninja (as for any other nanoFramework target).
- Python 3.10 or later, on the PATH. The build installs `west`, `kconfiglib` and the Zephyr/NCS Python requirements into that interpreter (set `NF_NORDIC_SKIP_PIP=ON` to manage them yourself).
- git.
- Optionally, an existing nRF Connect SDK west workspace and/or Zephyr SDK (see below). If none is given, **the build downloads them**.

## Build configuration

Target configuration follows the standard nanoFramework Kconfig workflow: each target has a `defconfig` referenced by its `CMakePresets.json` (`NF_TARGET_DEFCONFIG`). The Zephyr board is selected there with `CONFIG_NORDIC_ZEPHYR_BOARD`. Zephyr's own configuration lives in the usual Zephyr files in the target folder: `prj.conf`, `*.overlay` and optional `overlay-net.conf` (applied when building with networking).

Tool locations are CMake (not Kconfig) variables, so they go in `config/user-tools-repos.json` (see the template):

| Variable | Meaning |
|---|---|
| `NRF_CONNECT_SDK_SOURCE_FOLDER` | Existing NCS west workspace (the folder containing `zephyr`, `nrf`, …). If empty, NCS is downloaded with `west` into `NF_NORDIC_DEPS_DIR/ncs`. |
| `ZEPHYR_SDK_INSTALL_DIR` | Existing Zephyr SDK. If empty (and the `ZEPHYR_SDK_INSTALL_DIR` environment variable isn't set either), the minimal bundle plus the `arm-zephyr-eabi` toolchain are downloaded into `NF_NORDIC_DEPS_DIR/zephyr-sdk-<version>`. |
| `NF_NORDIC_DEPS_DIR` | Where downloads go. Defaults to `<build folder>/_deps`; point it somewhere persistent to keep the multi-GB NCS download across build folder cleanups. |

Note that the Zephyr build system supplies the toolchain (the Zephyr SDK GCC), so the Nordic presets don't set `CMAKE_TOOLCHAIN_FILE`.

Then build as usual:

```bash
cmake --preset NRF52840_DK
cmake --build --preset NRF52840_DK
```

The first configure can take a long time while `west update` clones the NCS repositories.

The Zephyr build tree ends up at `build/zephyr` (the Zephyr application is the top-level CMake project, so the board `CMakeLists.txt` is included from the root one rather than added as a subdirectory), and the resulting image is copied to `build/nanoCLR.hex` (and `nanoCLR.elf`). For the nRF9160 non-secure target that is `tfm_merged.hex`, i.e. TF-M plus nanoCLR.

## Adding a board

Target boards need a Zephyr board definition. Standard boards come with Zephyr/NCS; for a custom board add a [hardware model v2](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html) board directory and append its root to `BOARD_ROOT` in the target `CMakeLists.txt` before `nf_setup_target_build()`.

## Documentation

- Detailed documentation for the nRF Connect SDK can be found [here](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/index.html).
- More detailed information on the supported targets can be found in the target directories.

## nanoBooter

None of the Nordic targets implements nanoBooter. Instead the Zephyr application is flashed directly (for nRF9160 non-secure builds together with TF-M).

## Flashing nanoCLR

The resulting `nanoCLR.hex` can be programmed with a standard JTAG debugger into the hardware. The nRF Connect for Desktop tools include a "Programmer" application, and there's a "Flash Nordic Targets" task in the VS Code tasks template using J-Link.

Documentation for the programmer can be found [here](https://docs.nordicsemi.com/bundle/nrf-connect-programmer/page/index.html).

### Debugging

Each target folder has a `launch.json` for debugging nanoCLR over the DK's on-board J-Link with the VS Code C/C++ extension. Copy it to `.vscode/launch.json` and fill in the two placeholders: the Zephyr SDK folder (when the build downloads it, that's `build/_deps/zephyr-sdk-1.0.1`; the debugger is its `arm-zephyr-eabi-gdb`, not `arm-none-eabi-gdb`) and the J-Link install folder (e.g. `C:/Program Files/SEGGER/JLink`). The configuration programs the target from `build/nanoCLR.elf` with GDB's `load` (J-Link flash download) and starts the session.

For the nRF9160 the ELF only covers the non-secure nanoCLR application: flash `build/nanoCLR.hex` (TF-M plus nanoCLR) once with the "Flash Nordic Targets" task or the nRF Connect Programmer, after that the debug session only re-programs the nanoCLR partition and TF-M stays in place.

If the J-Link GDB server can't connect to the probe although Windows lists a J-Link device, run `JLink.exe` and `connect` once: the DK's Interface MCU firmware may need updating (the J-Link software does that on the first connection).
