#!/usr/bin/python
# Copyright 2013 Christian Berendt
# Copyright 2013 Luke Dashjr
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.  See COPYING for more details.

import argparse
import json
import logging
import pprint
import socket

logging.basicConfig(
         format='%(asctime)s %(levelname)s %(message)s',
         level=logging.DEBUG
)

parser = argparse.ArgumentParser()
parser.add_argument("command", default="summary", nargs='?')
parser.add_argument("parameter", default="", nargs='?')
parser.add_argument("--hostname", default="localhost")
parser.add_argument("--port", type=int, default=4028)
args = parser.parse_args()

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

try:
    s.connect((args.hostname, args.port))
except socket.error, e:
    logging.error(e)

try:
    s.send("{\"command\" : \"%s\", \"parameter\" : \"%s\"}"
            % (args.command, args.parameter)
          )
except socket.error, e:
    logging.error(e)


data = ''
while True:
    try:
        newdata = s.recv(1024)
        if newdata:
            data += newdata
        else:
            break
    except socket.error, e:
        break

try:
    s.close()
except socket.error,e:
    logging.error(e)

if data:
    data = json.loads(data.replace('\x00', ''))
    pp = pprint.PrettyPrinter()
    pp.pprint(data)
