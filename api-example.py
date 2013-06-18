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
api_command = raw_input("Enter Api Command: ")
api_param   = raw_input("Enter Api Param: ")
reply_command = raw_input("Enter Json Reply command: ")
reply_param = raw_input("Enter Json Reply Param: ")
s = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
s.connect(('192.168.1.6',4028))
s.send(json.dumps({"command":api_command,"parameter":api_param}))    
response = linesplit(s)
response = response.replace('\x00','')
response = json.loads(response)
#print response
print response[reply_command][0][reply_param]
s.close()
