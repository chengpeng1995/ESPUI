"""Select the board only when exactly one Espressif USB device is present."""
import sys
from serial.tools import list_ports

ports = [port.device for port in list_ports.comports() if port.vid == 0x303A]
if len(ports) != 1:
    print("Expected one Espressif USB port. Supply the desired port explicitly: "
          "./scripts/idf.sh flash /dev/cu.usbmodem101", file=sys.stderr)
    sys.exit(1)
print(ports[0])
