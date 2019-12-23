import serial

with serial.Serial('/dev/tty.usbmodem59983601') as teensy:
    while True:
        print(int.from_bytes(teensy.read(), byteorder='big'))
