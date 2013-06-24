#!/usr/bin/python

# author: Christian Berendt <berendt@b1-systems.de>

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


data = None
try:
    data = s.recv(1024)
except socket.error, e:
    logging.error(e)

try:
    s.close()
except socket.error,e:
    logging.error(e)

if data:
    data = json.loads(data.replace('\x00', ''))
    pp = pprint.PrettyPrinter()
    pp.pprint(data)
