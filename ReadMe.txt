Modbus on Sphere
================
This is split into two programs.
The high level A7 which will run the high level API and create the Modbus
payloads for commands and requests.
The A7 layer will allow creation of Modbus TCP and Modbus RTU connections.

Modbus TCP will use socket level communication on the A7 processor.

Modbus RTU will pass the payload to the M4 processor to send over a serial
link, using the Application_Socket communication library.

In the inital implementation, the M4 code will hardcode the serial port and
its configuration (e.g. baud rate, stop bits etc).
