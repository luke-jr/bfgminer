#!/usr/bin/env python2.7

# Copyright 2013 Setkeh Mkfr
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.  See COPYING for more details.

#Short Python Example for connecting to The Cgminer API
#Written By: setkeh <https://github.com/setkeh>
#Thanks to Jezzz for all his Support.
#NOTE: When adding a param with a pipe | in bash or ZSH you must wrap the arg in quotes
#E.G "pga|0"

import socket
import json
import sys

def linesplit(socket):
	buffer = socket.recv(4096)
	done = False
	while not done:
		more = socket.recv(4096)
		if not more:
			done = True
		else:
			buffer = buffer+more
	if buffer:
		return buffer

api_command = sys.argv[1].split('|')

if len(sys.argv) < 3:
	api_ip = '127.0.0.1'
	api_port = 4028
else:
	api_ip = sys.argv[2]
	api_port = sys.argv[3]

s = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
s.connect((api_ip,int(api_port)))
if len(api_command) == 2:
	s.send(json.dumps({"command":api_command[0],"parameter":api_command[1]}))
else:
	s.send(json.dumps({"command":api_command[0]}))

response = linesplit(s)
response = response.replace('\x00','')
response = json.loads(response)
print response
s.close()
