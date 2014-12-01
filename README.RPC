
This README contains details about the BFGMiner RPC API

It also includes some detailed information at the end,
about using miner.php


If you start BFGMiner with the "--api-listen" option, it will listen on a
simple TCP/IP socket for single string API requests from the same machine
running BFGMiner and reply with a string and then close the socket each time
If you add the "--api-network" option, it will accept API requests from any
network attached computer.

You can only access the commands that reply with data in this mode.
By default, you cannot access any privileged command that affects the miner -
you will receive an access denied status message instead. See --api-allow below
for more details.

You can specify IP addresses/prefixes that are only allowed to access the API
with the "--api-allow" option, e.g. --api-allow W:192.168.0.1,10.0.0/24
will allow 192.168.0.1 or any address matching 10.0.0.*, but nothing else.
IP addresses are automatically padded with extra '.0's as needed
Without a /prefix is the same as specifying /32.
0/0 means all IP addresses.
The 'W:' on the front gives that address/subnet privileged access to commands
that modify BFGMiner (thus all API commands).
Without it those commands return an access denied status.
See --api-groups below to define other groups like W:
Privileged access is checked in the order the IP addresses were supplied to
"--api-allow"
The first match determines the privilege level.
Using the "--api-allow" option overrides the "--api-network" option if they
are both specified
With "--api-allow", 127.0.0.1 is not by default given access unless specified

If you start BFGMiner also with the "--api-mcast" option, it will listen for
a multicast message and reply to it with a message containing it's API port
number, but only if the IP address of the sender is allowed API access.

More groups (like the privileged group W:) can be defined using the
--api-groups command
Valid groups are only the letters A-Z (except R & W are predefined) and are
not case sensitive.
The R: group is the same as not privileged access.
The W: group is (as stated) privileged access (thus all API commands).
To give an IP address/subnet access to a group you use the group letter
in front of the IP address instead of W: e.g. P:192.168.0/32
An IP address/subnet can only be a member of one group.
A sample API group would be:
 --api-groups
        P:switchpool:enablepool:addpool:disablepool:removepool:poolpriority:*
This would create a group 'P' that can do all current pool commands and all
non-privileged commands - the '*' means all non-privileged commands.
Without the '*' the group would only have access to the pool commands.
Defining multiple groups example:
 --api-groups Q:quit:restart:*,S:save
This would define 2 groups:
 Q: that can 'quit' and 'restart' as well as all non-privileged commands.
 S: that can only 'save' and no other commands.

The RPC API request can be either simple text or JSON.

If the request is JSON (starts with '{'), it will reply with a JSON formatted
response, otherwise it replies with text formatted as described further below.

The JSON request format required is '{"command":"CMD","parameter":"PARAM"}'
(though of course parameter is not required for all requests)
where "CMD" is from the "Request" column below and "PARAM" would be e.g.
the device number if required.

An example request in both formats to set device 0 fan to 80%:
  pgaset|0,fan,80
  {"command":"pgaset","parameter":"0,fan,80"}

The format of each reply (unless stated otherwise) is a STATUS section
followed by an optional detail section.

From API version 1.7 onwards, reply strings in JSON and Text have the
necessary escaping as required to avoid ambiguity - they didn't before 1.7.
For JSON the 2 characters '"' and '\' are escaped with a '\' before them.
For Text the 4 characters '|' ',' '=' and '\' are escaped the same way.

Only user entered information will contain characters that require being
escaped, such as Pool URL, User and Password or the Config save filename,
when they are returned in messages or as their values by the API.

For API version 1.4 and later:

The STATUS section is:

 STATUS=X,When=NNN,Code=N,Msg=string,Description=string|

  STATUS=X Where X is one of:
   W - Warning
   I - Informational
   S - Success
   E - Error
   F - Fatal (code bug)

  When=NNN
   Standard long time of request in seconds.

  Code=N
   Each unique reply has a unique Code (See api.c - #define MSG_NNNNNN).

  Msg=string
   Message matching the Code value N.

  Description=string
   This defaults to the BFGMiner version but is the value of --api-description
   if it was specified at runtime.

With API V3.1 you can also request multiple report replies in a single command
request
e.g. to request both summary and devs, the command would be summary+devs

This is only available for report commands that don't need parameters,
and is not available for commands that change anything
Any parameters supplied will be ignored

The extra formatting of the result is to have a section for each command
e.g. CMD=summary|STATUS=....|CMD=devs|STATUS=...
With JSON, each result is within a section of the command name
e.g. {"summary":{"STATUS":[{"STATUS":"S"...}],"SUMMARY":[...],"id":1},
      "devs":{"STATUS":[{"STATUS:"S"...}],"DEVS":[...],"id":1},"id":1}

As before, if you supply bad JSON you'll just get a single 'E' STATUS section
in the old format, since it doesn't switch to using the new format until it
correctly processes the JSON and can match a '+' in the command

If you request a command multiple times, e.g. devs+devs
you'll just get it once
If this results in only one command, it will still use the new layout
with just the one command

If you request a command that can't be used due to requiring parameters,
a command that isn't a report, or an invalid command, you'll get an 'E' STATUS
for that one but it will still attempt to process all other commands supplied

Blank/missing commands are ignore e.g. +devs++
will just show 'devs' using the new layout

For API version 1.10 and later:

The list of requests - a (*) means it requires privileged access - and replies:

 Request       Reply Section  Details
 -------       -------------  -------
 version       VERSION        Miner="BFGMiner " BFGMiner version
                              CGMiner=BFGMiner version
                              API=API version

 config        CONFIG         Some miner configuration information:
                              PGA Count=N, <- the number of PGAs
                              Pool Count=N, <- the number of Pools
                              ADL=X, <- Y or N if ADL is compiled in the code
                              ADL in use=X, <- Y or N if any GPU has ADL
                              Strategy=Name, <- the current pool strategy
                              Log Interval=N, <- log interval (--log N)
                              Device Code=GPU ICA , <- spaced list of compiled
                                                       device drivers
                              OS=Linux/Apple/..., <- operating System
                              Failover-Only=true/false, <- failover-only setting
                              ScanTime=N, <- --scan-time setting
                              Queue=N, <- --queue setting
                              Expiry=N, <- --expiry setting
                              Coinbase-Sig=X, <- extra coinbase data in blocks
                              ConfigFileN=X| <- filename of configs loaded

 summary       SUMMARY        The status summary of the miner
                              e.g. Elapsed=NNN,Found Blocks=N,Getworks=N,...|

 pools         POOLS          The status of each pool e.g.
                              Pool=0,URL=http://pool.com:6311,Status=Alive,...|

 devs          DEVS           Each available device with their status
                              e.g. PGA=0,Accepted=NN,MHS av=NNN,...,Intensity=D|
                              Last Share Time=NNN, <- standard long time in sec
                               (or 0 if none) of last accepted share
                              Last Share Pool=N, <- pool number (or -1 if none)
                              Last Valid Work=NNN, <- standand long time in sec
                               of last work returned that wasn't an HW:

 procs         DEVS           The details of each processor in the same format
                              and details as for DEVS

 devscan|info  DEVS           Probes for a device specified by info, which is
                              the same format as the --scan-serial command line
                              option

 pga|N         PGA            The details of a single PGA number N in the same
                              format and details as for DEVS
                              This is only available if PGA mining is enabled
                              Use 'pgacount' or 'config' first to see if there
                              are any

 proc|N        PGA            The details of a single processor number N in the
                              same format and details as for DEVS

 pgacount      PGAS           Count=N| <- the number of PGAs
                              Always returns 0 if PGA mining is disabled

 proccount     PGAS           Count=N| <- the number of processors

 switchpool|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of switching pool N to the
                              highest priority (the pool is also enabled)
                              The Msg includes the pool URL

 enablepool|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of enabling pool N
                              The Msg includes the pool URL

 addpool|URL,USR,PASS[,GOAL] (*)
               none           There is no reply section just the STATUS section
                              stating the results of attempting to add pool N
                              The Msg includes the pool URL
                              Use '\\' to get a '\' and '\,' to include a comma
                              inside URL, USR, PASS, or GOAL

 poolpriority|N,... (*)
               none           There is no reply section just the STATUS section
                              stating the results of changing pool priorities
                              See usage below

 poolquota|N,Q (*)
               none           There is no reply section just the STATUS section
                              stating the results of changing pool quota to Q

 disablepool|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of disabling pool N
                              The Msg includes the pool URL

 removepool|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of removing pool N
                              The Msg includes the pool URL
                              N.B. all details for the pool will be lost

 save|filename (*)
               none           There is no reply section just the STATUS section
                              stating success or failure saving the BFGMiner
                              config to filename
                              The filename is optional and will use the BFGMiner
                              default if not specified

 quit (*)      none           There is no reply section just the STATUS section
                              before BFGMiner quits

 notify        NOTIFY         The last status and history count of each devices
                              problem
                              e.g. NOTIFY=0,Name=PGA,ID=0,ProcID=0,Last Well=1332432290,...|

 privileged (*)
               none           There is no reply section just the STATUS section
                              stating an error if you do not have privileged
                              access to the API and success if you do have
                              privilege
                              The command doesn't change anything in BFGMiner

 pgaenable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the enable request
                              You cannot enable a PGA if its status is not WELL
                              This is only available if PGA mining is enabled

 pgadisable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the disable request
                              This is only available if PGA mining is enabled

 pgarestart|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the restart request

 pgaidentify|N (*)
               none           This is equivalent to PROCIDENTIFY on the first
                              processor of any given device
                              This is only available if PGA mining is enabled

 procenable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the enable request

 procdisable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the disable request

 procidentify|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the identify request
                              On most supported devices, it will flash the led
                              for approximately 4s
                              All unsupported devices, it will return a warning
                              status message stating that they don't support it
                              For BFL, this adds a 4s delay to the share being
                              processed so you may get a message stating that
                              processing took longer than 7000ms if the request
                              was sent towards the end of the timing of any work
                              being worked on
                              e.g.: BFL0: took 8438ms - longer than 7000ms
                              You should ignore this

 devdetails    DEVDETAILS     Each device with a list of their static details
                              This lists all devices including those not
                              supported by the 'devs' command
                              e.g. DEVDETAILS=0,Name=BFL,ID=0,ProcID=0,Driver=bitforce,...|

 restart (*)   none           There is no reply section just the STATUS section
                              before BFGMiner restarts

 stats         STATS          Each device or pool that has 1 or more getworks
                              with a list of stats regarding getwork times
                              The values returned by stats may change in future
                              versions thus would not normally be displayed
                              Device drivers are also able to add stats to the
                              end of the details returned

 check|cmd     COMMAND        Exists=Y/N, <- 'cmd' exists in this version
                              Access=Y/N| <- you have access to use 'cmd'

 failover-only|true/false (*)
               none           There is no reply section just the STATUS section
                              stating what failover-only was set to

 coin          COIN           Coin mining information:
                              Hash Method=sha256/scrypt,
                              Current Block Time=N.N, <- 0 means none
                              Current Block Hash=XXXX..., <- blank if none
                              LP=true/false, <- LP is in use on at least 1 pool
                              Network Difficulty=NN.NN|

 debug|setting (*)
               DEBUG          Debug settings
                              The optional commands for 'setting' are the same
                              as the screen curses debug settings
                              You can only specify one setting
                              Only the first character is checked - case
                              insensitive:
                              Silent, Quiet, Verbose, Debug, RPCProto,
                              PerDevice, WorkTime, Normal
                              The output fields are (as above):
                              Silent=true/false,
                              Quiet=true/false,
                              Verbose=true/false,
                              Debug=true/false,
                              RPCProto=true/false,
                              PerDevice=true/false,
                              WorkTime=true/false|

 setconfig|name,value (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting 'name'
                              The valid values for name are currently:
                              queue, scantime, expiry (integer in the range
                                                       0 to 9999)
                              coinbase-sig (string)

 pgaset|N,opt[,val] (*)
               none           This is equivalent to PROCSET on the first
                              processor of any given device
                              This is only available if PGA mining is enabled

 procset|N,opt[,val] (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting processor N with
                              opt[,val]

                              If the processor does not support any set options,
                              it will always return a WARN stating pgaset isn't
                              supported

                              If opt=help it will return an INFO status with a
                              help message about the options available

                              The current options are:
                               MMQ opt=clock val=2 to 250 (a multiple of 2)
                               XBS opt=clock val=2 to 250 (a multiple of 2)

 zero|Which,true/false (*)
               none           There is no reply section just the STATUS section
                              stating that the zero, and optional summary, was
                              done
                              If Which='all', all normal BFGMiner and API
                              statistics will be zeroed other than the numbers
                              displayed by the stats command
                              If Which='bestshare', only the 'Best Share' values
                              are zeroed for each pool and the global
                              'Best Share'
                              The true/false option determines if a full summary
                              is shown on the BFGMiner display like is normally
                              displayed on exit.

When you enable, disable or restart a device, you will also get Thread messages
in the BFGMiner status window.

The 'poolpriority' command can be used to reset the priority order of multiple
pools with a single command - 'switchpool' only sets a single pool to first
priority. Each pool should be listed by id number in order of preference (first
= most preferred). Any pools not listed will be prioritised after the ones that
are listed, in the priority order they were originally If the priority change
affects the miner's preference for mining, it may switch immediately.

When you switch to a different pool to the current one (including by priority
change), you will get a 'Switching to URL' message in the BFGMiner status
windows.

Obviously, the JSON format is simply just the names as given before the '='
with the values after the '='.

If you enable BFGMiner debug (--debug or using RPC), you will also get messages
showing some details of the requests received and the replies.

There are included 5 program examples for accessing the API:

api-example.php - a PHP script to access the API.
  usage: php api-example.php command
 by default it sends a 'summary' request to the miner at 127.0.0.1:4028
 If you specify a command it will send that request instead.
 You must modify the line "$socket = getsock('127.0.0.1', 4028);" at the
 beginning of "function request($cmd)" to change where it looks for BFGMiner.

api-example.c - a 'C' program to access the API (with source code).
  usage: api-example [command [ip/host [port]]]
 again, as above, missing or blank parameters are replaced as if you entered:
  api-example summary 127.0.0.1 4028

miner.php - an example web page to access the API.
 This includes buttons and inputs to attempt access to the privileged commands.
 See the end of this README.RPC for details of how to tune the display
 and also to use the option to display a multi-rig summary.

api-example.py - a Python script to access the API.
  usage: python api-example.py [--host HOST] [--port PORT] [command] [parameter]
 by default it sends a 'summary' request to the miner at 127.0.0.1:4028
 If you specify a command it will send that request instead.

api-example.rb - a Ruby script to access the API.
  usage: ruby api-example.rb command[:parameter] [HOST [PORT]]

If you are using Node.js, you can also use the miner-rpc package and script:
https://www.npmjs.org/package/miner-rpc

----------

Feature Changelog for external applications using the API:


API V3.3 (BFGMiner v5.0.0)

Modified API command:
 'addpool' - accept an additional argument to indicate mining goal by name
 'coin' - return multiple elements, when there are multiple mining goals
          defined; add 'Difficulty Accepted'
 'pools' - add 'Mining Goal'

---------

API V3.2 (BFGMiner v4.1.0)

Modified API command:
 'config' - add 'ConfigFile'N

---------

API V3.1 (BFGMiner v4.0.0)

Multiple report request command with '+' e.g. summary+devs

CPU and OpenCL devices are now included as "PGAs", to enable migration to a simpler interface.

Added API commands:
 'pgarestart'

Modified API commands:
 'devs' - remove 'GPU Count' and 'CPU Count'
 'quit' - expand reply to include a complete STATUS section
 'restart' - expand reply to include a complete STATUS section
 'summary' - add 'MHS rolling'
 'version' - add 'Miner'

Deprecated API commands:
 'cpu'
 'cpucount'
 'cpuenable'
 'cpudisable'
 'cpurestart'
 'gpu'
 'gpucount'
 'gpuenable'
 'gpudisable'
 'gpurestart'
 'gpuintensity'
 'gpumem'
 'gpuengine'
 'gpufan'
 'gpuvddc'

---------

API V2.3 (BFGMiner v3.7.0)

Modified API command:
 'devdetails' - Add 'Processors', 'Manufacturer', 'Product', 'Serial',
                    'Target Temperature', 'Cutoff Temperature'
 'procdetails' - Add 'Manufacturer', 'Product', 'Serial', 'Target Temperature',
                     'Cutoff Temperature'

---------

API V2.2 (BFGMiner v3.6.0)

Modified API command:
 'pools' - add 'Works'

---------

API V2.1 (BFGMiner v3.4.0)

Added API command:
 'poolquota' - Set pool quota for load-balance strategy.

Modified API command:
 'devs', 'gpu', 'pga', 'procs' and 'asc' - add 'Device Elapsed', 'Stale',
                                             'Work Utility', 'Difficulty Stale'
 'pools' - add 'Quota'
 'summary' - add 'Diff1 Work', 'MHS %ds' (where %d is the log interval)

---------

API V2.0 (BFGMiner v3.3.0)

Removed API commands:
 'devdetail' - Use newer 'devdetails' for same information.

Modified API commands:
 'devs' - display status of each full device only (not processors)
 'pga' - lookup and display device by device (not processor) number
 'pgacount' - count only full devices (not processors)
 'pgaenable' - enable all processors for a numbered full device
 'pgadisable' - disable all processors for a numbered full device
 'pgaidentify' - choose first processor of numbered full device
 'pgaset' - choose first processor of numbered full device

Added API commands:
 'procs'
 'proc'
 'proccount'
 'procenable'
 'procdisable'
 'procidentify'
 'procset'

----------

API V1.25.3 (BFGMiner v3.2.0)

Modified API commands:
 'devs', 'pga', 'gpu' - add 'Device Hardware%' and 'Device Rejected%'
 'pools' - add 'Pool Rejected%' and 'Pool Stale%'
 'setconfig' - add 'http-port' number
 'summary' - add 'Device Hardware%', 'Device Rejected%', 'Pool Rejected%',
                 'Pool Stale%'

Removed output limitation:
 All replies can now be longer than the previous limitation of 64k, and will
  only be truncated on a 50ms timeout sending.

Basic support for cgminer-compatible multicast RPC detection added.

----------

API V1.25.2 (BFGMiner v3.1.4)

Modified API commands:
 'pgaset' - added: XBS opt=clock val=2 to 250 (and a multiple of 2)

----------

API V1.25.1 (BFGMiner v3.1.2)

Added API commands:
 'devscan'

----------

API V1.25 (BFGMiner v3.0.1)

Modified API commands:
 'devs' 'gpu' and 'pga' - add 'Last Valid Work'

----------

API V1.24.1 (BFGMiner v3.0.0)

Modified API commands:
 'cpustatus' - add 'ProcID'
 'gpustatus' - add 'ProcID'
 'pgastatus' - add 'ProcID'
 'devstatus' - add 'ProcID'
 'notify' - add 'ProcID'
 'devdetails' - add 'ProcID'
 'devdetail' - add 'Name', 'ID', and 'ProcID'
 'pools' - add 'Message'
 'coin' - add 'Network Difficulty'

Pretty much updated every method returning 'Name' and 'ID' to also return
'ProcID'. This is a number starting with 0 for 'a', 1 for 'b', etc.

----------

API V1.24 (BFGMiner v2.10.3)

Added API commands:
 'zero'

Modified API commands:
 'pools' - add 'Best Share'
 'stats' - rename 'Bytes Sent' and 'Bytes Recv' to 'Net Bytes Sent' and
                  'Net Bytes Recv'

----------

API V1.23 (BFGMiner v2.10.1)

Added API commands:
 'pgaset' - with: MMQ opt=clock val=2 to 230 (and a multiple of 2)

----------

API V1.22 (not released)

Enforced output limitation:
 all extra records beyond the output limit of the API (~64k) are ignored and
  chopped off at the record boundary before the limit is reached however, JSON
  brackets will be correctly closed and the JSON id will be set to 0 (instead
  of 1) if any data was truncated.

Modified API commands:
 'stats' - add 'Times Sent', 'Bytes Sent', 'Times Recv', 'Bytes Recv'

----------

API V1.21 (BFGMiner v2.10.0)

Modified API commands:
 'summary' - add 'Best Share'

----------

API V1.20b (BFGMiner v2.9.1)

Support for the X6500 FPGA was added.

----------

API V1.20 (BFGMiner v2.9.0)

Modified API commands:
 'pools' - add 'Has Stratum', 'Stratum Active', 'Stratum URL'

----------

API V1.19b (BFGMiner v2.8.1)

Added API commands:
 'pgaidentify|N' (only works for BitForce Singles so far)

Modified API commands:
 Change pool field name back from 'Diff1 Work' to 'Diff1 Shares'
 'devs' - add 'Difficulty Accepted', 'Difficulty Rejected',
              'Last Share Difficulty' to all devices
 'gpu|N' - add 'Difficulty Accepted', 'Difficulty Rejected',
              'Last Share Difficulty'
 'pga|N' - add 'Difficulty Accepted', 'Difficulty Rejected',
              'Last Share Difficulty'
 'notify' - add '*Dev Throttle' (for BitForce Singles)
 'pools' - add 'Difficulty Accepted', 'Difficulty Rejected',
               'Difficulty Stale', 'Last Share Difficulty'
 'stats' - add 'Work Diff', 'Min Diff', 'Max Diff', 'Min Diff Count',
               'Max Diff Count' to the pool stats
 'setconfig|name,value' - add 'Coinbase-Sig' string

----------

API V1.19 (BFGMiner v2.8.0)

Added API commands:
 'debug'
 'setconfig|name,N'

Modified API commands:
 Change pool field name 'Diff1 Shares' to 'Diff1 Work'
 'devs' - add 'Diff1 Work' to all devices
 'gpu|N' - add 'Diff1 Work'
 'pga|N' - add 'Diff1 Work'
 'pools' - add 'Proxy'
 'config' - add 'Queue', 'Expiry'

----------

API V1.18 (BFGMiner v2.7.4)

Modified API commands:
 'stats' - add 'Work Had Roll Time', 'Work Can Roll', 'Work Had Expire',
           and 'Work Roll Time' to the pool stats
 'config' - include 'ScanTime'

----------

API V1.17b (BFGMiner v2.7.1)

Modified API commands:
 'summary' - add 'Work Utility'
 'pools' - add 'Diff1 Shares'

----------

API V1.17 (BFGMiner v2.6.5)

Added API commands:
 'coin'

----------

API V1.16 (BFGMiner v2.6.5)

Added API commands:
 'failover-only'

Modified API commands:
 'config' - include failover-only state

----------

API V1.15 (BFGMiner v2.5.2)

Added API commands:
 'poolpriority'

----------

API V1.14 (BFGMiner v2.5.0)

Modified API commands:
 'stats' - more Icarus timing stats added
 'notify' - include new device comms error counter

The internal code for handling data was rewritten (~25% of the code)
Completely backward compatible

----------

API V1.13 (BFGMiner v2.4.4)

Added API commands:
 'check'

Support was added to BFGMiner for API access groups with the --api-groups option
It's 100% backward compatible with previous --api-access commands

----------

API V1.12 (BFGMiner v2.4.3)

Modified API commands:
 'stats' - more pool stats added

Support for the ModMiner FPGA was added

----------

API V1.11 (BFGMiner v2.4.2)

Modified API commands:
 'save' no longer requires a filename (use default if not specified)

'save' incorrectly returned status E (error) on success before.
It now correctly returns S (success)

----------

API V1.10 (BFGMiner v2.4.1)

Added API commands:
 'stats'

N.B. the 'stats' command can change at any time so any specific content
present should not be relied upon.
The data content is mainly used for debugging purposes or hidden options
in BFGMiner and can change as development work requires.

Modified API commands:
 'pools' added "Last Share Time"

----------

API V1.9 (BFGMiner v2.4.0)

Added API commands:
 'restart'

Modified API commands:
 'notify' corrected invalid JSON

----------

API V1.8 (BFGMiner v2.3.5)

Added API commands:
 'devdetails'

Support for the ZTEX FPGA was added.

----------

API V1.8-pre (BFGMiner v2.3.4)

Added API commands:
 'devdetail'

----------

API V1.7 (BFGMiner v2.3.4)

Added API commands:
 'removepool'

Modified API commands:
 'pools' added "User"

From API version 1.7 onwards, reply strings in JSON and Text have the
necessary escaping as required to avoid ambiguity.
For JSON the 2 characters '"' and '\' are escaped with a '\' before them.
For Text the 4 characters '|' ',' '=' and '\' are escaped the same way.

----------

API V1.6 (cgminer v2.3.2)

Added API commands:
 'pga'
 'pgaenable'
 'pgadisable'
 'pgacount'

Modified API commands:
 'devs' now includes Icarus and BitForce FPGA devices.
 'notify' added "*" to the front of the name of all numeric error fields.
 'config' correct "Log Interval" to use numeric (not text) type for JSON.

Support for Icarus and BitForce FPGAs was added.

----------

API V1.5 was not released

----------

API V1.4 (Kano's interim release of cgminer v2.3.1)

Added API commands:
 'notify'

Modified API commands:
 'config' added "Device Code" and "OS"

Added "When" to the STATUS reply section of all commands.

----------

API V1.3 (cgminer v2.3.1-2)

Added API commands:
 'addpool'

Modified API commands:
 'devs'/'gpu' added "Total MH" for each device
 'summary' added "Total MH"

----------

API V1.2 (cgminer v2.3.0)

Added API commands:
 'enablepool'
 'disablepool'
 'privileged'

Modified API commands:
 'config' added "Log Interval"

Starting with API V1.2, any attempt to access a command that requires
privileged security, from an IP address that does not have privileged
security, will return an "Access denied" Error Status.

----------

API V1.1 (cgminer v2.2.4)

There were no changes to the API commands in cgminer v2.2.4,
however support was added to cgminer for IP address restrictions
with the --api-allow option.

----------

API V1.1 (cgminer v2.2.2)

Prior to V1.1, devs/gpu incorrectly reported GPU0 Intensity for all GPUs.

Modified API commands:
 'devs'/'gpu' added "Last Share Pool" and "Last Share Time" for each device

----------

API V1.0 (cgminer v2.2.0)

Remove default CPU support.

Added API commands:
 'config'
 'gpucount'
 'cpucount'
 'switchpool'
 'gpuintensity'
 'gpumem'
 'gpuengine'
 'gpufan'
 'gpuvddc'
 'save'

----------

API V0.7 (cgminer v2.1.0)

Initial release of the API in the main cgminer git

Commands:
 'version'
 'devs'
 'pools'
 'summary'
 'gpuenable'
 'gpudisable'
 'gpurestart'
 'gpu'
 'cpu'
 'gpucount'
 'cpucount'
 'quit'

----------------------------------------

miner.php
=========

miner.php is a PHP based interface to the BFGMiner RPC API
(referred to simply as the API below).

It can show rig details, summaries and input fields to allow you to change
BFGMiner.
You can also create custom summary pages with it

It has two levels to the security:
1) BFGMiner can be configured to allow or disallow API access and access level
   security for miner.php
2) miner.php can be configured to allow or disallow privileged BFGMiner
   access, if BFGMiner is configured to allow privileged access for miner.php

---------

To use miner.php requires a web server with PHP.

Basics: On Xubuntu 11.04, to install Apache and PHP, the commands are:
 sudo apt-get install apache2
 sudo apt-get install php5
 sudo /etc/init.d/apache2 reload

On Fedora 17:
 yum install httpd php
 systemctl restart httpd.service
 systemctl enable httpd.service --system

On windows there are a few options.
Try one of these (apparently the first one is easiest - thanks jborkl)
 http://www.easyphp.org/
 http://www.apachefriends.org/en/xampp.html
 http://www.wampserver.com/en/

---------

The basic BFGMiner option to enable the API is:

 --api-listen

or in your bfgminer.conf:

 "api-listen" : true,

(without the ',' on the end if it is the last item.)

If the web server is running on the BFGMiner computer, the above
is the only change required to give miner.php basic access to
the BFGMiner API.

-

If the web server runs on a different computer to BFGMiner,
you will also need to tell BFGMiner to allow the web server
to access BFGMiner's API and tell miner.php where BFGMiner is.

Assuming a.b.c.d is the IP address of the web server, you
would add the following to BFGMiner:

 --api-listen --api-allow a.b.c.d

or in your bfgminer.conf:

 "api-listen" : true,
 "api-allow" : "a.b.c.d",

to tell BFGMiner to give the web server read access to the API.

You also need to tell miner.php where BFGMiner is.
Assuming BFGMiner is at IP address e.f.g.h, then you would
edit miner.php and change the line:

 $rigs = array('127.0.0.1:4028');

to

 $rigs = array('e.f.g.h:4028');

See --api-network or --api-allow for more access details
and how to give write access.

You can however, also tell miner.php to find your mining rigs automatically
on the local subnet.

Add the following to each BFGMiner:

 --api-mcast

or in your bfgminer.conf:

 "api-mcast" : true,

And in miner.php set $mcast = true;

This will ignore the value of $rigs and overwrite it with the list of zero or
more rigs found on the network in the timeout specified.
A rig will not reply if the API settings would mean it would also ignore an
API request from the web server running miner.php

---------

Once you have a web server with PHP running:

 copy your miner.php to the main web folder

On Xubuntu 11.04:
 /var/www/

On Fedora 17:
 /var/www/html/

On Windows:
 Please check your windows Web/PHP documentation.

Assuming the IP address of the web server is a.b.c.d
Then in your web browser go to:

 http://a.b.c.d/miner.php

Done :)

---------

The rest of this documentation deals with the more complex
functions of miner.php, using myminer.php, creating custom
summaries and displaying multiple BFGMiner rigs.

---------

If you create a file called myminer.php in the same web folder
where you put miner.php, miner.php will load it when it runs.

This is useful, to put any changes you need to make to miner.php
instead of changing miner.php.
Thus if you update/get a new miner.php, you won't lose the changes
you have made if you put all your changes in myminer.php
(and haven't changed miner.php at all)

A simple example myminer.php that defines 2 rigs
(that I will keep referring to further below) is:

<?php
#
$rigs = array('192.168.0.100:4028:A', '192.168.0.102:4028:B');
#
?>

Changes in myminer.php supersede what is in miner.php
However, this is only valid for variables in miner.php before the
2 lines where myminer.php is included by miner.php:

 if (file_exists('myminer.php'))
  include_once('myminer.php');
 
Every variable in miner.php above those 2 lines, can be changed by
simply defining them in your myminer.php

So although miner.php originally contains the line:

 $rigs = array('127.0.0.1:4028');

if you created the example myminer.php given above, it would actually
change the value of $rigs that is used when miner.php is running.
i.e. you don't have to remove or comment out the $rigs line in miner.php
It will be superseded by myminer.php

---------

The example myminer.php above also shows how to define more that one rig
to be shown my miner.php:

Each rig string is 2 or 3 values separated by colons ':'
They are simply an IP address or hostname, followed by the
port number (usually 4028) and an optional Name string.

miner.php displays rig buttons that will show the details of a single
rig when you click on it - the button shows either the rig number,
or the 'Name' string if you provide it.

PHP arrays contain each string separated by a comma, but no comma after
the last one.

So an example for 3 rigs would be:

 $rigs = array('192.168.0.100:4028:A', '192.168.0.102:4028:B',
               '192.168.0.110:4028:C');

Of course each of the rigs listed would also have to have the API
running and be set to allow the web server to access the API - as
covered earlier in this document.

---------

So basically, any variable explained below can be put in myminer.php if you want
to set it to something different to its default value and did not want to change
miner.php itself every time you update it.

Below is a list of the variables that can be changed and an explanation of each.

---------

Default:
 $dfmt = 'H:i:s j-M-Y \U\T\CP';

Define the date format used to print full length dates.
If you get the string 'UTCP' on the end of your dates shown, that
means you are using an older version of PHP and you can instead use:
 $dfmt = 'H:i:s j-M-Y \U\T\CO';

The PHP documentation on the date format is here:
 http://us.php.net/manual/en/function.date.php

---------

Default:
 $title = 'Mine';

Web page title.
If you know PHP you can of course use code to define it e.g.
 $title = 'My Rig at: '.date($dfmt);

Which would set the web page title to something like:
 My Rig at: 10:34:00 22-Aug-2012 UTC+10:00

---------

Default:
 $readonly = false;

Set $readonly to true to force miner.php to be readonly.
This means it won't allow you to change BFGMiner even if the RPC API
options allow it to.

If you set $readonly to false then it will check BFGMiner 'privileged'
and will show input fields and buttons on the single rig page,
allowing you to change devices, pools and even quit or restart
BFGMiner.

However, if the 'privileged' test fails, the code will set $readonly to
true.

---------

Default:
 $userlist = null;

Define password checking and default access null means there is no password
checking.

$userlist is an array of 3 arrays, e.g.
$userlist = array('sys' => array('boss' => 'bpass'),
                  'usr' => array('user' => 'upass', 'pleb' => 'ppass'),
                  'def' => array('Pools'));

'sys' is an array of system users and passwords (full access).
'usr' is an array of user level users and passwords (readonly access).
'def' is an array of custompages that anyone not logged in can view.

Any of the 3 can be null, meaning there are none of that item.

All validated 'usr' users are given $readonly = true; access.
All validated 'sys' users are given the $readonly access you defined.

If 'def' has one or more values, and allowcustompages is true, then anyone
without a password can see the list of custompage buttons given in 'def' and
will see the first one when they go to the web page, with a login button at the
top right.

From the login page, if you login with no username or password, it will show
the first 'def' custompage (if there are any).

If you are logged in, it will show a logout button at the top right.

---------

Default:
 $notify = true;

Set $notify to false to NOT attempt to display the notify command table of data

Set $notify to true to attempt to display the notify command on the single rig
page.
If your older version of BFGMiner returns an 'Invalid command' because it
doesn't have notify - it just shows the error status table.

---------

Default:
 $checklastshare = true;

Set $checklastshare to true to do the following checks:
If a device's last share is 12x expected ago then display as an error.
If a device's last share is 8x expected ago then display as a warning.
If either of the above is true, also display the whole line highlighted
This assumes shares are 1 difficulty shares.

Set $checklastshare to false to not do the above checks.

'expected' is calculated from the device Mh/s value.
So for example, a device that hashes at 380Mh/s should (on average) find a
share every 11.3s.
If the last share was found more than 11.3 x 12 seconds (135.6s) ago, it is
considered an error and highlighted.
If the last share was found more than 11.3 x 8 seconds (90.4s) ago, it is
considered a warning and highlighted.

The default highlighting is very subtle, so change it if you want it to be more
obvious.

---------

Default:
 $poolinputs = false;

Set $poolinputs to true to show the input fields for adding a pool and changing
the pool priorities on a single rig page.
However, if $readonly is true, it will not display them.

---------

Default:
 $rigport = 4028;

Default port to use if any $rigs entries don't specify the port number

---------

Default:
 $rigs = array('127.0.0.1:4028');

Set $rigs to an array of your BFGMiner rigs that are running format: 'IP' or
 'Host' or 'IP:Port' or 'Host:Port' or 'Host:Port:Name'.
If you only have one rig, it will just show the detail of that rig.
If you have more than one rig it will show a summary of all the rigs with
 buttons to show the details of each rig - the button contents will be 'Name'
 rather than rig number, if you specify 'Name'.
If Port is missing or blank, it will try $rigport
e.g. $rigs = array('127.0.0.1:4028','myrig.com:4028:Sugoi');

---------

Default:
 $rignames = false;

Set $rignames to false to not affect the display.
Set $rignames to one of 'ip' or 'ipx' to alter the name displayed
if the rig doesn't have a 'name' in $rigs
Currently:
 'ip' means use the 4th byte of the rig IP address as an integer
 'ipx' means use the 4th byte of the rig IP address as 2 hex bytes

---------

Default:
 $rigbuttons = true;

Set $rigbuttons to false to display a link rather than a button on
the left of any summary table with rig buttons, in order to reduce
the height of the table cells

---------

Default:
 $mcast = false;

Set $mcast to true to look for your rigs and ignore $rigs.

---------

Default:
 $mcastexpect = 0;

The minimum number of rigs expected to be found when $mcast is true.
If fewer are found, an error will be included at the top of the page.

---------

Default:
 $mcastaddr = '224.0.0.75';

API Multicast address all miners are listening on.

---------

Default:
 $mcastport = 4028;

API Multicast UDP port all miners are listening on.

---------

Default:
 $mcastcode = 'FTW';

The code all miners expect in the Multicast message sent.
The message sent is "cgm-code-listport".
Don't use the '-' character if you change it.

---------

Default:
 $mcastlistport = 4027;

UDP port number that is added to the broadcast message sent
that specifies to the miners the port to reply on.

---------

Default:
 $mcasttimeout = 1.5;

Set $mcasttimeout to the number of seconds (floating point)
to wait for replies to the Multicast message.
N.B. the accuracy of the timing used to wait for the replies is
~0.1s so there's no point making it more than one decimal place.

---------

Default:
 $mcastretries = 0;

Set $mcastretries to the number of times to retry the multicast.

If $mcastexpect is 0, this is simply the number of extra times
that it will send the multicast request.
N.B. BFGMiner doesn't listen for multicast requests for 1000ms after
each one it hears.

If $mcastexpect is > 0, it will stop looking for replies once it
has found at least $mcastexpect rigs, but it only checks this rig
limit each time it reaches the $mcasttimeout limit, thus it can find
more than $mcastexpect rigs if more exist.
It will send the multicast message up to $mcastretries extra times or
until it has found at least $mcastexpect rigs.
When using $mcastretries, it is however possible for it to sometimes
ignore some rigs on the network if $mcastexpect is less than the
number of rigs on the network and some rigs are too slow to reply.

---------

Default:
 $allowgen = false;

Set $allowgen to true to allow customsummarypages to use 'gen',
false means ignore any 'gen' options.
This is disabled by default due to the possible security risk
of using it, please see the end of this document for an explanation.

---------

Default:
 $rigipsecurity = true;

Set $rigipsecurity to false to show the IP/Port of the rig in the socket error
 messages and also show the full socket message.

---------

Default:
 $rigtotals = true;
 $forcerigtotals = false;

Set $rigtotals to true to display totals on the single rig page, 'false' means
 no totals (and ignores $forcerigtotals).

If $rigtotals is true, all data is also right aligned.
With false, it's as before, left aligned.

This option is just here to allow people to set it to false if they prefer the
 old non-total display when viewing a single rig.

Also, if there is only one line shown in any section, then no total will be
 shown (to save screen space).
You can force it to always show rig totals on the single rig page, even if
 there is only one line, by setting $forcerigtotals = true;

---------

Default:
 $socksndtimeoutsec = 10;
 $sockrcvtimeoutsec = 40;

The numbers are integer seconds.

The defaults should be OK for most cases.
However, the longer SND is, the longer you have to wait while PHP hangs if the
target BFGMiner isn't running or listening.

RCV should only ever be relevant if BFGMiner has hung but the API thread is
still running, RCV would normally be >= SND.

Feel free to increase SND if your network is very slow or decrease RCV if that
happens often to you.

Also, on some windows PHP, apparently the $usec is ignored (so usec can't be
specified).

---------

Default:
 $hidefields = array();

List of fields NOT to be displayed.
You can use this to hide data you don't want to see or don't want shown on a
public web page.
The list of sections are:
 SUMMARY, POOL, PGA, GPU, NOTIFY, CONFIG, DEVDETAILS, DEVS
See the web page for the list of field names (the table headers).
It is an array of 'SECTION.Field Name' => 1

This example would hide the slightly more sensitive pool information:
Pool URL and pool username:
 $hidefields = array('POOL.URL' => 1, 'POOL.User' => 1);

If you just want to hide the pool username:
 $hidefields = array('POOL.User' => 1);

---------

Default:
 $ignorerefresh = false;
 $changerefresh = true;
 $autorefresh = 0;

Auto-refresh of the page (in seconds) - integers only.

$ignorerefresh = true/false always ignore refresh parameters.
$changerefresh = true/false show buttons to change the value.
$autorefresh = default value, 0 means don't auto-refresh.

---------

Default:
 $placebuttons = 'top';

Where to place the Refresh, Summary, Custom Pages, Quit, etc. buttons.

Valid values are: 'top' 'bot' 'both'
 Anything else means don't show them. (case sensitive)

---------

Default:
 $miner_font_family = 'verdana,arial,sans';
 $miner_font_size = '13pt';

Change these to set the font and font size used on the web page.

---------

Default:
 $colouroverride = array();

Use this to change the web page colour scheme.

See $colourtable in miner.php for the list of possible names to change.

Simply put in $colouroverride, just the colours you wish to change.

e.g. to change the colour of the header font and background
you could do the following:

 $colouroverride = array(
	'td.h color'		=> 'green',
	'td.h background'	=> 'blue'
 );

---------

Default:
 $allowcustompages = true;

Should we allow custom pages?
(or just completely ignore them and don't display the buttons.)

---------

OK this part is more complex: Custom Summary Pages.

A custom summary page in an array of 'section' => array('FieldA','FieldB'...)

The section defines what data you want in the summary table and the Fields
define what data you want shown from that section.

Standard sections are:
 SUMMARY, POOL, PGA, GPU, NOTIFY, CONFIG, DEVDETAILS, DEVS, STATS, COIN

Fields are the names as shown on the headers on the normal pages.

Fields can be 'name=new name' to display 'name' with a different heading
'new name'.

There are also now joined sections:
 SUMMARY+POOL, SUMMARY+DEVS, SUMMARY+CONFIG, DEVS+NOTIFY, DEVS+DEVDETAILS
 SUMMARY+COIN

These sections are an SQL join of the two sections and the fields in them
are named section.field where 'section.' is the section the field comes from
See the example further down.

Also note:
- empty tables are not shown.
- empty columns (e.g. an unknown field) are not shown.
- missing field data shows as blank.
- the field name '*' matches all fields except in joined sections
  (useful for STATS and COIN).

There are 2 hard coded sections:
 DATE - displays a date table like at the start of 'Summary'.
 RIGS - displays a rig table like at the start of 'Summary'.

Each custom summary requires a second array, that can be empty, listing fields
to be totalled for each section.
If there is no matching total data, no total will show.

---------

Looking at the Mobile example:

 $mobilepage = array(
  'DATE' => null,
  'RIGS' => null,
  'SUMMARY' => array('Elapsed', 'MHS av', 'Found Blocks=Blks', 
			Accepted', 'Rejected=Rej', 'Utility'),
  'DEVS+NOTIFY' => array('DEVS.Name=Name', 'DEVS.ID=ID', 'DEVS.ProcID=Proc',
			'DEVS.Status=Status',
			'DEVS.Temperature=Temp', 'DEVS.MHS av=MHS av',
			'DEVS.Accepted=Accept', 'DEVS.Rejected=Rej',
			'DEVS.Utility=Utility', 'NOTIFY.Last Not Well=Not Well'),
  'POOL' => array('POOL', 'Status', 'Accepted', 'Rejected=Rej',
                  'Last Share Time'));

 $mobilesum = array(
  'SUMMARY' => array('MHS av', 'Found Blocks', 'Accepted', 'Rejected',
                     'Utility'),
  'DEVS+NOTIFY' => array('DEVS.MHS av', 'DEVS.Accepted', 'DEVS.Rejected',
                         'DEVS.Utility'),
  'POOL' => array('Accepted', 'Rejected'));

 $customsummarypages = array('Mobile' => array($mobilepage, $mobilesum));

This will show 5 tables (according to $mobilepage).
Each table will have the chosen details for all the rigs specified in $rigs

 DATE
	A single box with the web server's current date and time.

 RIGS
	A table of the rigs: description, time, versions etc.

 SUMMARY

	This will use the API 'summary' command and show the selected fields:
		Elapsed, MHS av, Found Blocks, Accepted, Rejected and Utility
	However, 'Rejected=Rej' means that the header displayed for the 'Rejected'
	field will be 'Rej', instead of 'Rejected' (to save space).
	Same for 'Found Blocks=Blks' - to save space.

 DEVS+NOTIFY

	This will list each of the devices on each rig and display the list of
	fields as shown.
	It will also include the 'Last Not Well' field from the 'notify' command
	so you know when the device was last not well.

	You will notice that you need to rename each field e.g. 'DEVS.Name=Name'
	since each field name in the join between DEVS and NOTIFY is actually
	section.fieldname, not just fieldname.

	The join code automatically adds 2 fields to each GPU device: 'Name', 'ID',
	and 'ProcID'. They don't exist in the API 'devs' output but we can correctly
	calculate them from the GPU device data. These two fields are used to join
	DEVS to NOTIFY: i.e. find the NOTIFY record that has the same Name/ID/ProcID
	as the DEVS record and join them.

 POOL

	This will use the API 'pools' command and show the selected fields:
		POOL, Status, Accepted, Rejected, Last Share Time
	Again, I renamed the 'Rejected' field using 'Rejected=Rej', to save space.

$mobilesum lists the sections and fields that should have a total.
You can't define them for 'DATE' or 'RIGS' since they are hard coded tables.
The example given:

 SUMMARY
	Show a total at the bottom of the columns for:
		MHS av, Found Blocks, Accepted, Rejected, Utility

	Firstly note that you use the original name i.e. for 'Rejected=Rej'
	you use 'Rejected', not 'Rej' and not 'Rejected=Rej'.

	Secondly note that it simply adds up the fields.
	If you ask for a total of a string field you will get the numerical
	sum of the string data.

 DEVS+NOTIFY

	Simply note in this join example that you must use the original field
	names which are section.fieldname, not just fieldname.

 POOL
	Show a total at the bottom of the columns for:
		Accepted and Rejected

	Again remember to use the original field name 'Rejected'.

---------

With BFGMiner 2.10.1 and later, miner.php includes an extension to the custom
pages that allows you to apply SQL style commands to the data: where, group,
and having
BFGMiner 3.4.0 also includes another option 'gen'.

As an example, miner.php includes a more complex custom page called 'Pools'
which includes the extension:

$poolsext = array(
 'POOL+STATS' => array(
        'where' => null,
        'group' => array('POOL.URL', 'POOL.Has Stratum',
                         'POOL.Stratum Active'),
        'calc' => array('STATS.Bytes Sent' => 'sum',
                        'STATS.Bytes Recv' => 'sum'),
        'gen' => array('AvShr', 'POOL.Difficulty Accepted/max(POOL.Accepted,1)),
        'having' => array(array('STATS.Bytes Recv', '>', 0)))
);

This allows you to group records together from one or more rigs.
In the example, you'll get each Pool (with the same URL+Stratum info) listed
once for all rigs and a sum of each of the fields listed in 'calc'.


'where' and 'having' are an array of fields and restrictions to apply.

In the above example, it will only display the rows where it contains the
'STATS.Bytes Recv' field with a value greater than zero.
If the row doesn't have the field, it will always be included.
All restrictions must be true in order for the row to be included.
Any restiction that is invalid or unknown is true.
An empty array, or null, means there are no restrictions.

A restriction is formatted as: array('Field', 'restriction', 'value')
Field is the simple field name as normally displayed, or SECTION.Field if it is
a joined section (as in this case 'POOL+STATS').
The list of restrictions are:
'set' - true if the row contains the 'Field' ('value' is not required or used)
'=', '<', '<=', '>', '>' - a numerical comparison.
'eq', 'lt', 'le', 'gt', 'ge' - a case insensitive string comparison.

You can have multiple restrictions on a 'Field' - but all must be true to
include the row containing the 'Field'.
e.g. a number range between 0 and 10 would be:
array('STATS.Bytes Recv', '>', 0), array('STATS.Bytes Recv', '<', 10)

The difference between 'where' and 'having' is that 'where' is applied to the
data before grouping it and 'having' is applied to the data after grouping it
- otherwise they work the same.


'group' lists the fields to group over and 'calc' lists the function to apply
to other fields that are not part of 'group'.

You can only see fields listed in 'group' and 'calc'.

A 'calc' is formatted as: 'Field' => 'function'
The current list of operations available for 'calc' are:
'sum', 'avg', 'min', 'max', 'lo', 'hi', 'count', 'any'
The first 4 are as expected - the numerical sum, average, minimum or maximum.
'lo' is the first string of the list, sorted ignoring case.
'hi' is the last string of the list, sorted ignoring case.
'count' is the number of rows in the section specified in the calc e.g.
 ('DEVS.Name' => 'count') would be the number of DEVS selected in the 'where'
 of course any valid 'DEVS.Xyz' would give the same 'count' value.
'any' is effectively random: the field value in the 1st row of the grouped data.
An unrecognised 'function' uses 'any'.

A 'gen' allows you to generate new fields from any php valid function of any
of the other fields.
 e.g. 'gen' => array('AvShr', 'POOL.Difficulty Accepted/max(POOL.Accepted,1)),
will generate a new field called GEN.AvShr that is the function shown, which
in this case is the average difficulty of each share submitted.

THERE IS A SECURITY RISK WITH HOW GEN WORKS!
It simply replaces all the variables with their values and then requests PHP
to execute the formula - thus if a field value returned from a BFGMiner API
request contained PHP code, it could be executed by your web server.
Of course BFGMiner doesn't do this, but if you do not control the BFGMiner that
returns the data in the API calls, someone could modify BFGMiner to return a
PHP string in a field you use in 'gen'.
Thus use 'gen' at your own risk.
If someone feels the urge to write a mathematical interpreter in PHP to get
around this risk, feel free to write one and submit it to the API author for
consideration.
