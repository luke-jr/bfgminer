#!/usr/bin/env python2.7

#Short Python Example for connecting to The Cgminer API
#Written By: setkeh <https://github.com/setkeh>

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
#api_ip = sys.argv[3]
#api_port = sys.argv[4]

if len(sys.argv) < 3:
	api_ip = '127.0.0.1'
	api_port = 4028
else:
	api_ip = sys.argv[2]
	api_port = sys.argv[3]

print api_ip, api_port
s = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
s.connect((api_ip,int(api_port)))
s.send(json.dumps({"command":api_command[0],"parameter":api_command[1]}))    
response = linesplit(s)
response = response.replace('\x00','')
response = json.loads(response)
print response
s.close()
