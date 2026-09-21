# Nordic Semiconductor 9160DK

The development kit board for the Nordic Semiconductor nrf9160.

## Documentation

- Detailed documentation for this target reference can be found [here](https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk).

- More detailed information on building and flashing the .NET NF on Nordic devices, see the [readme](../README.md).

- This build expects the 9160DK to be set in its configuration as described in the 9160DK User Guide.

- This build has been tested using the nRF Connect SDK v3.4.1 (Zephyr 4.4) and Zephyr SDK 1.0.1. See the [platform readme](../README.md) for the build pre-requisites and configuration.

- This build has been tested on 9160DK version 0.8.2. There are changes to the newer versions of this board but these aren't expected to impact the functionality in this build.

- This build target only supports the nrf9160 on the 9160DK. While there is an nrf52840 on the 9160DK board, it is used for as the board controller, not the standard target. Refer to the 52840DK target for builds targeting the nrf52840.

- NOTE: Network support using the cellular connectivity function on the nrf9160 is not supported in this initial release. It is targeted for support in the near future.

## Board resources used in this build

The Wire Protocol (Visual Studio debugger) uses UART0, which is routed to the interface MCU
virtual COM port (VCOM0, the "JLink CDC UART Port"). Connect Visual Studio to that COM port.

The Zephyr console output (`printk`) goes to SEGGER RTT over the J-Link connection, use
J-Link RTT Viewer (or `JLinkRTTClient`) to see it.

The LEDs are available via GPIO:
    LED      GPIO Pin   
     1          2
     2          3
     3          4
     4          5

The buttons are available via GPIO
   Button    GPIO pin
     1          6
     2          7

Other GPIOs are exposed on the board in Arduino-style connectors and are 
clearly labelled on the board.

## I2C

I2C functionality is working in this build. By default, I2C1 is exposed through the Arduino compatible connector.

## Floating point

The current build is set to add support for single-precision floating point.
Meaning that `System.Math` API supports only the `float` overloads. The `double` ones will throw a `NotImplementedException`.

## Flash layout

This is a non-secure build (`nrf9160dk/nrf9160/ns`): TF-M is built as the secure image and merged with nanoCLR into `nanoCLR.hex`. The layout is set in `nrf9160dk_nrf9160_ns.overlay` and mirrored in `common/Device_BlockStorage.c`:

| Range | Use |
|---|---|
| 0x00000 - 0x50000 | TF-M (secure image) |
| 0x50000 - 0x90000 | nanoCLR (256 KB) |
| 0x90000 - 0xF0000 | managed code deployment (384 KB) |
| 0xF0000 - 0x100000 | TF-M storage partitions and Zephyr storage partition |

The non-secure application only gets 128 KB of RAM (the rest is used by TF-M and the modem shared memory), so the system heap is smaller on this target than on the nRF52840 DK.

## nanoBooter

Not implemented, see the flash layout above.

## Flashing nanoCLR

The 9160DK hardware contains integrated JTAG debugging hardware. The micro-USB connector at the end of the board is used for programming/debugging the 9160 and provides logging output.
