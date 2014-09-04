#!/usr/bin/env ruby

# Copyright 2014 James Hilliard
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.  See COPYING for more details.

require 'socket'
require 'json'

api_command = ARGV[0].split(":")

if ARGV.length == 3
	api_ip = ARGV[1]
	api_port = ARGV[2]
elsif ARGV.length == 2
	api_ip = ARGV[1]
	api_port = 4028
else
	api_ip = "127.0.0.1"
	api_port = 4028
end

s = TCPSocket.open(api_ip, api_port)

if api_command.count == 2
	s.write({ :command => api_command[0], :parameter => api_command[1]}.to_json)
else
	s.write({ :command => api_command[0]}.to_json)
end

response = s.read.strip
response = JSON.parse(response)

puts response
s.close
