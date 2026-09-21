# Nordic Semiconductor 52840DK

The development kit board for the Nordic Semiconductor nrf52840.

## Documentation

- Detailed documentation for this target reference can be found [here](https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK).

- More detailed information on building and flashing the .NET NF on Nordic devices, see the [readme](../README.md)..

- This build expects the 52840DK to be set in its configuration as described in the User Guide.

- This build has been tested using the nRF Connect SDK v3.4.1 (Zephyr 4.4) and Zephyr SDK 1.0.1. See the [platform readme](../README.md) for the build pre-requisites and configuration.

- This build has been tested on 52840DK version 1.1.0.

## Board resources used in this build:

The Wire Protocol (Visual Studio debugger) uses UART0, which is routed to the interface MCU
virtual COM port (VCOM0, the "JLink CDC UART Port"). Connect Visual Studio to that COM port.

The Zephyr console output (`printk`) goes to SEGGER RTT over the J-Link connection, use
J-Link RTT Viewer (or `JLinkRTTClient`) to see it.

The LEDs are available via GPIO:
    LED      GPIO Pin
     1          13
     2          14
     3          15
     4          16

The buttons are available via GPIO
   Button    GPIO pin (default)
     1          11
     2          12
     3          24
     4          25

Other GPIOs are exposed on the board in Arduino-style connectors and are clearly labelled on the board.

## I2C

I2C functionality is working in this build. However, an I2C bus must be declared in an .overlay file and CONFIG_I2C=y must be set in the prj.conf for the bus to be instantiated by the Zephyr OS.

## Floating point

The current build is set to add support for single-precision floating point.
Meaning that `System.Math` API supports only the `float` overloads. The `double` ones will throw a `NotImplementedException`.

## Flash layout

There is no bootloader: nanoCLR (the Zephyr application) is linked at the start of flash. The layout is set in `nrf52840dk_nrf52840.overlay` and mirrored in `common/Device_BlockStorage.c`:

| Range | Use |
|---|---|
| 0x00000 - 0x60000 | nanoCLR (384 KB) |
| 0x60000 - 0xF8000 | managed code deployment (608 KB) |
| 0xF8000 - 0x100000 | Zephyr storage partition |

GPIO pin numbers are `port * 32 + pin`, so P0.x is `x` and P1.x is `32 + x`.

## nanoBooter

Not implemented, see the flash layout above.

## Flashing nanoCLR

The 52840DK hardware contains integrated JTAG debugging hardware. There are two USB connectors on the 52840DK. The micro-USB connector at the end of the board is used for programming/debugging the 52840 and provides logging output. The micro-USB on the bottom side of the board is used for testing the USB functionality of the 52840. nanoFramework support for USB connections via this connector are not supported at this time.
