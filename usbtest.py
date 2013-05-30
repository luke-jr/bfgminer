#!/usr/bin/env python2.7
#
# Original version supplied to me (Kano/kanoi) by xiangfu
#
# Modified to allow supplying the data to send
#
# Linux usAge: ./ubstest.py /dev/ttyUSB0 0xhexcodes|string|icarus
#  OR		python ubstest.py /dev/ttyUSB0 0xhexcodes|string|icarus
#
# Windows usAge: ./ubstest.py COM1 0xhexcodes|string|icarus
#
#   sends the data sepcified to the USB device and waits
#   for a reply then displays it
#
#   the data can be:
#	0xhexcodes: e.g. 0x68656c6c6f20776f726c640a
#			would send "hello world\n"
#
#	string: e.g. sendsometext
#
#	icarus: sends 2 known block payloads for an icarus device
#		and shows the expected and actual answers if it's
#		a working V3 icarus

import sys
import serial
import binascii

if len(sys.argv) < 2:
	sys.stderr.write("usAge: " + sys.argv[0] + " device strings...\n")
	sys.stderr.write(" where device is either like /dev/ttyUSB0 or COM1\n")
	sys.stderr.write(" and strings are either '0xXXXX' or 'text'\n")
	sys.stderr.write(" if the first string is 'icarus' the rest are ignored\n")
	sys.stderr.write("  and 2 valid icarus test payloads are sent with results displayed\n")
	sys.stderr.write("\nAfter any command is sent it waits up to 30 seconds for a reply\n");
	sys.exit("Aborting")

# Open with a 10 second timeout - just to be sure
ser = serial.Serial(sys.argv[1], 115200, serial.EIGHTBITS, serial.PARITY_NONE, serial.STOPBITS_ONE, 10, False, False, 5, False, None)

if sys.argv[2] == "icarus":

	# This show how Icarus use the block and midstate data
	# This will produce nonce 063c5e01
	block = "0000000120c8222d0497a7ab44a1a2c7bf39de941c9970b1dc7cdc400000079700000000e88aabe1f353238c668d8a4df9318e614c10c474f8cdf8bc5f6397b946c33d7c4e7242c31a098ea500000000000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000"
	midstate = "33c5bf5751ec7f7e056443b5aee3800331432c83f404d9de38b94ecbf907b92d"

	rdata2  = block.decode('hex')[95:63:-1]
	rmid    = midstate.decode('hex')[::-1]
	payload = rmid + rdata2

	print("Push payload to icarus: " + binascii.hexlify(payload))
	ser.write(payload)

	b=ser.read(4)
	print("Result:(should be: 063c5e01): " + binascii.hexlify(b))

	# Just another test
	payload2 = "ce92099c5a80bb81c52990d5c0924c625fd25a535640607d5a4bdf8174e2c8d500000000000000000000000080000000000000000b290c1a42313b4f21b5bcb8"
	print("Push payload to icarus: " + payload2)
	ser.write(payload2.decode('hex'))

	b=ser.read(4)
	print("Result:(should be: 8e0b31c5): " + binascii.hexlify(b))
else:
	data = ""
	for arg in sys.argv[2::]:
		if arg[0:2:] == '0x':
			data += arg[2::].decode('hex')
		else:
			data += arg

	print("Sending: 0x" + binascii.hexlify(data))
	ser.write(data)

	# If you're expecting more than one linefeed terminated reply,
	# you'll only see the first one
	# AND with no linefeed, this will wait the 10 seconds before returning
	print("Waiting up to 10 seconds ...")
	b=ser.readline()
	print("Result: hex 0x" + binascii.hexlify(b))

	# This could mess up the display - do it last
	print("Result: asc '" + b + "'")

ser.close()
