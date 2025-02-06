Bluetooth: Peripheral UART with beacon
######################################

Based on the official Bluetooth: Peripheral UART sample:
https://github.com/nrfconnect/sdk-nrf/tree/main/samples/bluetooth/peripheral_uart

The sample has two main modes, CONNECTED and DISCONNECTED.

- LED0 (nRF54) / LED1 (nRF52) is blinks when advertising in DISCONNECTED mode (see back side of DK for pin number).
- LED1 (nRF54) / LED2 (nRF52) is turned on when in CONNECTED mode. This functions as status pin.

DISCONNECTED mode
*****************

In DISCONNECTED mode, the sample advertises connectable with NUS with a fixed advertising packet and
a dynamic scan response packet. The default name is "NB". The scan response packet content is
written via UART when no BLE connect is active. Write the packet content as a string terminated by a
newline. Maximum string length is 27 bytes. To stop transmitting scan response data, send an empty
UART string containing only a line break (CR or LF) or another single character transaction.

The scan response data is reset/disabled when entering CONNECTED mode to avoid advertising old scan
response data after entering DISCONNECTED mode.

Format of the Scan response packet:
 - 2 byte Company ID (required by the Bluetooth specification in manufacturer specific data)
 - Up to 27 bytes of payload

CONNECTED mode
***************

In CONNECTED mode, the sample provides a data tunnel between the connected central/client and the
local UART interface.

Testing
*******
Testign with only a DK and a mobile
===================================

 1. Scan with nRF Connect for mobile. The device advertises as "NB".
 2. Connect a UART terminal without flow control and 9600 baud. Write some characters and press enter. Observe that the data is now advertised (in the scan response).
 3. Connect to the device. Now Nordic UART Service (NUS) is used. Subscribe to the Tx characteristic in nRF Connect and observe that data written in the UART terminal is sent to the phone after pressing enter (line break).
 4. Send a notification from the phone on the Rx characteristic and observe that it is printed in the UART terminal.

Testign with external hardware
==============================
 
nRF 54L15 DK:
- By default the UART pins are connected to the onboard debugger. To connect externally, first disconnect VCOM1 from debugger in Board Configurator.
- Set the VDD (nPM VOUT1) voltage to match what you are connecting to (between 1.8V and 3.3V) in Board Configurator.
- UART Tx pin is P1.04. UART RX pin is P1.05.

nRF52 DK:
- If using nRF52 series DK, flip the nRF ONLY / DEFAULT swich to nRF ONLY. Then use the UART pins from the pin header. See back side of DK for UART Tx/Rx pin numbers.

Notes
*****
 - The default company identifier is Nordic. Set ``CONFIG_BT_COMPANY_ID`` in prj.conf to modify.
 - The advertising interval can be configured by setting ``BT_NUS_ADVERTISING_INTERVAL`` in prj.conf to a value in milliseconds.
 - Tested with nRF Connect SDK 2.9.0.
 - Disable CONFIG_BT_NUS_CRLF_UART_TERMINATION to not need a CR or LF to terminate UART transactions.
