
This README contains details about the cgminer RPC API

It also includes some detailed information at the end,
about using miner.php


If you start cgminer with the "--api-listen" option, it will listen on a
simple TCP/IP socket for single string API requests from the same machine
running cgminer and reply with a string and then close the socket each time
If you add the "--api-network" option, it will accept API requests from any
network attached computer.

You can only access the comands that reply with data in this mode.
By default, you cannot access any privileged command that affects the miner -
you will receive an access denied status message see --api-allow below.

You can specify IP addresses/prefixes that are only allowed to access the API
with the "--api-allow" option e.g. --api-allow W:192.168.0.1,10.0.0/24
will allow 192.168.0.1 or any address matching 10.0.0.*, but nothing else
IP addresses are automatically padded with extra '.0's as needed
Without a /prefix is the same as specifying /32
0/0 means all IP addresses.
The 'W:' on the front gives that address/subnet privileged access to commands
that modify cgminer (thus all API commands)
Without it those commands return an access denied status.
See --api-groups below to define other groups like W:
Privileged access is checked in the order the IP addresses were supplied to
"--api-allow"
The first match determines the privilege level.
Using the "--api-allow" option overides the "--api-network" option if they
are both specified
With "--api-allow", 127.0.0.1 is not by default given access unless specified

If you start cgminer also with the "--api-mcast" option, it will listen for
a multicast message and reply to it with a message containing it's API port
number, but only if the IP address of the sender is allowed API access

More groups (like the privileged group W:) can be defined using the
--api-groups command
Valid groups are only the letters A-Z (except R & W are predefined) and are
not case sensitive
The R: group is the same as not privileged access
The W: group is (as stated) privileged access (thus all API commands)
To give an IP address/subnet access to a group you use the group letter
in front of the IP address instead of W: e.g. P:192.168.0/32
An IP address/subnet can only be a member of one group
A sample API group would be:
 --api-groups
        P:switchpool:enablepool:addpool:disablepool:removepool:poolpriority:*
This would create a group 'P' that can do all current pool commands and all
non-priviliged commands - the '*' means all non-priviledged commands
Without the '*' the group would only have access to the pool commands
Defining multiple groups example:
 --api-groups Q:quit:restart:*,S:save
This would define 2 groups:
 Q: that can 'quit' and 'restart' as well as all non-priviledged commands
 S: that can only 'save' and no other commands

The RPC API request can be either simple text or JSON.

If the request is JSON (starts with '{'), it will reply with a JSON formatted
response, otherwise it replies with text formatted as described further below.

The JSON request format required is '{"command":"CMD","parameter":"PARAM"}'
(though of course parameter is not required for all requests)
where "CMD" is from the "Request" column below and "PARAM" would be e.g.
the ASC/GPU number if required.

An example request in both formats to set GPU 0 fan to 80%:
  gpufan|0,80
  {"command":"gpufan","parameter":"0,80"}

The format of each reply (unless stated otherwise) is a STATUS section
followed by an optional detail section

From API version 1.7 onwards, reply strings in JSON and Text have the
necessary escaping as required to avoid ambiguity - they didn't before 1.7
For JSON the 2 characters '"' and '\' are escaped with a '\' before them
For Text the 4 characters '|' ',' '=' and '\' are escaped the same way

Only user entered information will contain characters that require being
escaped, such as Pool URL, User and Password or the Config save filename,
when they are returned in messages or as their values by the API

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
   Standard long time of request in seconds

  Code=N
   Each unique reply has a unigue Code (See api.c - #define MSG_NNNNNN)

  Msg=string
   Message matching the Code value N

  Description=string
   This defaults to the cgminer version but is the value of --api-description
   if it was specified at runtime.

For API version 1.10 and later:

The list of requests - a (*) means it requires privileged access - and replies:

 Request       Reply Section  Details
 -------       -------------  -------
 version       VERSION        CGMiner=cgminer, version
                              API=API| version

 config        CONFIG         Some miner configuration information:
                              GPU Count=N, <- the number of GPUs
                              ASC Count=N, <- the number of ASCs
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
                              Expiry=N| <- --expiry setting

 summary       SUMMARY        The status summary of the miner
                              e.g. Elapsed=NNN,Found Blocks=N,Getworks=N,...|

 pools         POOLS          The status of each pool e.g.
                              Pool=0,URL=http://pool.com:6311,Status=Alive,...|

 devs          DEVS           Each available GPU, PGA and ASC with their details
                              e.g. GPU=0,Accepted=NN,MHS av=NNN,...,Intensity=D|
                              Last Share Time=NNN, <- standand long time in sec
                               (or 0 if none) of last accepted share
                              Last Share Pool=N, <- pool number (or -1 if none)
                              Last Valid Work=NNN, <- standand long time in sec
                               of last work returned that wasn't an HW:
                              Will not report PGAs if PGA mining is disabled
                              Will not report ASCs if ASC mining is disabled

 gpu|N         GPU            The details of a single GPU number N in the same
                              format and details as for DEVS

 pga|N         PGA            The details of a single PGA number N in the same
                              format and details as for DEVS
                              This is only available if PGA mining is enabled
                              Use 'pgacount' or 'config' first to see if there
                              are any

 gpucount      GPUS           Count=N| <- the number of GPUs

 pgacount      PGAS           Count=N| <- the number of PGAs
                              Always returns 0 if PGA mining is disabled

 switchpool|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of switching pool N to the
                              highest priority (the pool is also enabled)
                              The Msg includes the pool URL

 enablepool|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of enabling pool N
                              The Msg includes the pool URL

 addpool|URL,USR,PASS (*)
               none           There is no reply section just the STATUS section
                              stating the results of attempting to add pool N
                              The Msg includes the pool URL
                              Use '\\' to get a '\' and '\,' to include a comma
                              inside URL, USR or PASS

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

 gpuenable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the enable request

 gpudisable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the disable request

 gpurestart|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the restart request

 gpuintensity|N,I (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting GPU N intensity
                              to I

 gpumem|N,V (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting GPU N memoryclock
                              to V MHz

 gpuengine|N,V (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting GPU N clock
                              to V MHz

 gpufan|N,V (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting GPU N fan speed
                              to V%

 gpuvddc|N,V (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting GPU N vddc to V

 save|filename (*)
               none           There is no reply section just the STATUS section
                              stating success or failure saving the cgminer
                              config to filename
                              The filename is optional and will use the cgminer
                              default if not specified

 quit (*)      none           There is no status section but just a single "BYE"
                              reply before cgminer quits

 notify        NOTIFY         The last status and history count of each devices
                              problem
                              This lists all devices including those not
                              supported by the 'devs' command e.g.
                              NOTIFY=0,Name=GPU,ID=0,Last Well=1332432290,...|

 privileged (*)
               none           There is no reply section just the STATUS section
                              stating an error if you do not have privileged
                              access to the API and success if you do have
                              privilege
                              The command doesn't change anything in cgminer

 pgaenable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the enable request
                              You cannot enable a PGA if it's status is not WELL
                              This is only available if PGA mining is enabled

 pgadisable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the disable request
                              This is only available if PGA mining is enabled

 pgaidentify|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the identify request
                              This is only available if PGA mining is enabled
                              and currently only BFL singles and Cairnsmore1's
                              with the appropriate firmware support this command
                              On a BFL single it will flash the led on the front
                              of the device for appoximately 4s
                              All other non BFL,ICA PGA devices will return a
                              warning status message stating that they dont
                              support it. Non-CMR ICAs will ignore the command.
                              This adds a 4s delay to the BFL share being
                              processed so you may get a message stating that
                              procssing took longer than 7000ms if the request
                              was sent towards the end of the timing of any work
                              being worked on
                              e.g.: BFL0: took 8438ms - longer than 7000ms
                              You should ignore this

 devdetails    DEVDETAILS     Each device with a list of their static details
                              This lists all devices including those not
                              supported by the 'devs' command
                              e.g. DEVDETAILS=0,Name=GPU,ID=0,Driver=opencl,...|

 restart (*)   none           There is no status section but just a single
                              "RESTART" reply before cgminer restarts

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

 setconfig|name,N (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting 'name' to N
                              The valid values for name are currently:
                              queue, scantime, expiry
                              N is an integer in the range 0 to 9999

 usbstats      USBSTATS       Stats of all LIBUSB mining devices except ztex
                              e.g. Name=MMQ,ID=0,Stat=SendWork,Count=99,...|

 pgaset|N,opt[,val] (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting PGA N with
                              opt[,val]
                              This is only available if PGA mining is enabled

                              If the PGA does not support any set options, it
                              will always return a WARN stating pgaset isn't
                              supported

                              If opt=help it will return an INFO status with a
                              help message about the options available

                              The current options are:
                               MMQ opt=clock val=160 to 230 (a multiple of 2)
                               CMR opt=clock val=100 to 220

 zero|Which,true/false (*)
               none           There is no reply section just the STATUS section
                              stating that the zero, and optional summary, was
                              done
                              If Which='all', all normal cgminer and API
                              statistics will be zeroed other than the numbers
                              displayed by the usbstats and stats commands
                              If Which='bestshare', only the 'Best Share' values
                              are zeroed for each pool and the global
                              'Best Share'
                              The true/false option determines if a full summary
                              is shown on the cgminer display like is normally
                              displayed on exit.

 hotplug|N (*) none           There is no reply section just the STATUS section
                              stating that the hotplug setting succeeded
                              If the code is not compiled with hotplug in it,
                              the the warning reply will be
                               'Hotplug is not available'
                              If N=0 then hotplug will be disabled
                              If N>0 && <=9999, then hotplug will check for new
                              devices every N seconds

 asc|N         ASC            The details of a single ASC number N in the same
                              format and details as for DEVS
                              This is only available if ASC mining is enabled
                              Use 'asccount' or 'config' first to see if there
                              are any

 ascenable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the enable request
                              You cannot enable a ASC if it's status is not WELL
                              This is only available if ASC mining is enabled

 ascdisable|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the disable request
                              This is only available if ASC mining is enabled

 ascidentify|N (*)
               none           There is no reply section just the STATUS section
                              stating the results of the identify request
                              This is only available if ASC mining is enabled
                              and currently only BFL ASICs support this command
                              On a BFL single it will flash the led on the front
                              of the device for appoximately 4s
                              All other non BFL ASIC devices will return a
                              warning status message stating that they dont
                              support it

 asccount      ASCS           Count=N| <- the number of ASCs
                              Always returns 0 if ASC mining is disabled

 ascset|N,opt[,val] (*)
               none           There is no reply section just the STATUS section
                              stating the results of setting ASC N with
                              opt[,val]
                              This is only available if ASC mining is enabled

                              If the ASC does not support any set options, it
                              will always return a WARN stating ascset isn't
                              supported

                              If opt=help it will return an INFO status with a
                              help message about the options available

                              The current options are:
                               AVA+BTB opt=freq val=256 to 1024 - chip frequency
                               BTB opt=millivolts val=1000 to 1400 - corevoltage

 lockstats (*) none           There is no reply section just the STATUS section
                              stating the results of the request
                              A warning reply means lock stats are not compiled
                              into cgminer
                              The API writes all the lock stats to stderr

When you enable, disable or restart a GPU, PGA or ASC, you will also get
Thread messages in the cgminer status window

The 'poolpriority' command can be used to reset the priority order of multiple
pools with a single command - 'switchpool' only sets a single pool to first
priority
Each pool should be listed by id number in order of preference (first = most
preferred)
Any pools not listed will be prioritised after the ones that are listed, in the
priority order they were originally
If the priority change affects the miner's preference for mining, it may switch
immediately

When you switch to a different pool to the current one (including by priority
change), you will get a 'Switching to URL' message in the cgminer status
windows

Obviously, the JSON format is simply just the names as given before the '='
with the values after the '='

If you enable cgminer debug (-D or --debug) or, when cgminer debug is off,
turn on debug with the API command 'debug|debug' you will also get messages
showing some details of the requests received and the replies

There are included 4 program examples for accessing the API:

api-example.php - a php script to access the API
  usAge: php api-example.php command
 by default it sends a 'summary' request to the miner at 127.0.0.1:4028
 If you specify a command it will send that request instead
 You must modify the line "$socket = getsock('127.0.0.1', 4028);" at the
 beginning of "function request($cmd)" to change where it looks for cgminer

API.java/API.class
 a java program to access the API (with source code)
  usAge is: java API command address port
 Any missing or blank parameters are replaced as if you entered:
  java API summary 127.0.0.1 4028

api-example.c - a 'C' program to access the API (with source code)
  usAge: api-example [command [ip/host [port]]]
 again, as above, missing or blank parameters are replaced as if you entered:
  api-example summary 127.0.0.1 4028

miner.php - an example web page to access the API
 This includes buttons and inputs to attempt access to the privileged commands
 See the end of this API-README for details of how to tune the display
 and also to use the option to display a multi-rig summary

----------

Feature Changelog for external applications using the API:


API V1.32 (cgminer v3.6.5)

Modified API commands:
 'devs' 'gpu' 'pga' and 'asc' - add 'Device Elapsed'

---------

API V1.31 (cgminer v3.6.3)

Added API command:
 'lockstats' - display cgminer dev lock stats if compiled in

Modified API command:
 'summary' - add 'MHS %ds' (where %d is the log interval)

---------

API V1.30 (cgminer v3.4.3)

Added API command:
 'poolquota' - Set pool quota for load-balance strategy.

Modified API command:
 'pools' - add 'Quota'

---------

API V1.29 (cgminer v3.4.1)

Muticast identification added to the API

----------

API V1.28 (cgminer v3.3.4)

Modified API commands:
 'devs', 'pga', 'asc', 'gpu' - add 'Device Hardware%' and 'Device Rejected%'
 'pools' - add 'Pool Rejected%' and 'Pool Stale%'
 'summary' - add 'Device Hardware%', 'Device Rejected%', 'Pool Rejected%',
                 'Pool Stale%'

----------

API V1.27 (cgminer v3.3.2)

Added API commands:
 'ascset' - with: BTB opt=millivolts val=1000 to 1310 - core voltage
                  AVA+BTB opt=freq val=256 to 450 - chip frequency

----------

API V1.26 (cgminer v3.2.3)

Remove all CPU support (cgminer v3.0.0)

Added API commands:
 'asc'
 'ascenable'
 'ascdisable'
 'ascidentify|N' (only works for BFL ASICs so far)
 'asccount'

Various additions to the debug 'stats' command

----------

API V1.25

Added API commands:
 'hotplug'

Modified API commands:
 'devs' 'gpu' and 'pga' - add 'Last Valid Work'
 'devs' - list ASIC devices
 'config' - add 'Hotplug', 'ASC Count'
 'coin' - add 'Network Difficulty'

----------

API V1.24 (cgminer v2.11.0)

Added API commands:
 'zero'

Modified API commands:
 'pools' - add 'Best Share'
 'devs' and 'pga' - add 'No Device' for PGAs if MMQ or BFL compiled
 'stats' - add pool: 'Net Bytes Sent', 'Net Bytes Recv'

----------

API V1.23 (cgminer v2.10.2)

Added API commands:
 'pgaset' - with: MMQ opt=clock val=160 to 230 (and a multiple of 2)

----------

API V1.22 (cgminer v2.10.1)

Enforced output limitation:
 all extra records beyond the output limit of the API (~64k) are ignored
  and chopped off at the record boundary before the limit is reached
  however, JSON brackets will be correctly closed and the JSON id will be
  set to 0 (instead of 1) if any data was truncated

Modified API commands:
 'stats' - add 'Times Sent', 'Bytes Sent', 'Times Recv', 'Bytes Recv'

----------

API V1.21 (cgminer v2.10.0)

Added API commands:
 'usbstats'

Modified API commands:
 'summary' - add 'Best Share'

Modified output:
 each MMQ shows up as 4 devices, each with it's own stats

----------

API V1.20 (cgminer v2.8.5)

Modified API commands:
 'pools' - add 'Has Stratum', 'Stratum Active', 'Stratum URL'

----------

API V1.19 (cgminer v2.7.6)

Added API commands:
 'debug'
 'pgaidentify|N' (only works for BFL Singles so far)
 'setconfig|name,N'

Modified API commands:
 'devs' - add 'Diff1 Work', 'Difficulty Accepted', 'Difficulty Rejected',
              'Last Share Difficulty' to all devices
 'gpu|N' - add 'Diff1 Work', 'Difficulty Accepted',
              'Difficulty Rejected', 'Last Share Difficulty'
 'pga|N' - add 'Diff1 Work', 'Difficulty Accepted',
              'Difficulty Rejected', 'Last Share Difficulty'
 'notify' - add '*Dev Throttle' (for BFL Singles)
 'pools' - add 'Proxy Type', 'Proxy', 'Difficulty Accepted',
               'Difficulty Rejected', 'Difficulty Stale',
               'Last Share Difficulty'
 'config' - add 'Queue', 'Expiry'
 'stats' - add 'Work Diff', 'Min Diff', 'Max Diff', 'Min Diff Count',
               'Max Diff Count' to the pool stats

----------

API V1.18 (cgminer v2.7.4)

Modified API commands:
 'stats' - add 'Work Had Roll Time', 'Work Can Roll', 'Work Had Expire',
		'Work Roll Time' to the pool stats
 'config' - include 'ScanTime'

----------

API V1.17 (cgminer v2.7.1)

Added API commands:
 'coin'

Modified API commands:
 'summary' - add 'Work Utility'
 'pools' - add 'Diff1 Shares'

----------

API V1.16 (cgminer v2.6.5)

Added API commands:
 'failover-only'

Modified API commands:
 'config' - include failover-only state

----------

API V1.15 (cgminer v2.6.1)

Added API commands:
 'poolpriority'

----------

API V1.14 (cgminer v2.5.0)

Modified API commands:
 'stats' - more icarus timing stats added
 'notify' - include new device comms error counter

The internal code for handling data was rewritten (~25% of the code)
Completely backward compatible

----------

API V1.13 (cgminer v2.4.4)

Added API commands:
 'check'

Support was added to cgminer for API access groups with the --api-groups option
It's 100% backward compatible with previous --api-access commands

----------

API V1.12 (cgminer v2.4.3)

Modified API commands:
 'stats' - more pool stats added

Support for the ModMinerQuad FPGA was added

----------

API V1.11 (cgminer v2.4.2)

Modified API commands:
 'save' no longer requires a filename (use default if not specified)

'save' incorrectly returned status E (error) on success before.
It now correctly returns S (success)

----------

API V1.10 (cgminer v2.4.1)

Added API commands:
 'stats'

N.B. the 'stats' command can change at any time so any specific content
present should not be relied upon.
The data content is mainly used for debugging purposes or hidden options
in cgminer and can change as development work requires

Modified API commands:
 'pools' added "Last Share Time"

----------

API V1.9 (cgminer v2.4.0)

Added API commands:
 'restart'

Modified API commands:
 'notify' corrected invalid JSON

----------

API V1.8 (cgminer v2.3.5)

Added API commands:
 'devdetails'

Support for the ZTex FPGA was added

----------

API V1.7 (cgminer v2.3.4)

Added API commands:
 'removepool'

Modified API commands:
 'pools' added "User"

From API version 1.7 onwards, reply strings in JSON and Text have the
necessary escaping as required to avoid ambiguity
For JSON the 2 characters '"' and '\' are escaped with a '\' before them
For Text the 4 characters '|' ',' '=' and '\' are escaped the same way

----------

API V1.6 (cgminer v2.3.2)

Added API commands:
 'pga'
 'pgaenable'
 'pgadisable'
 'pgacount'

Modified API commands:
 'devs' now includes Icarus and Bitforce FPGA devices
 'notify' added "*" to the front of the name of all numeric error fields
 'config' correct "Log Interval" to use numeric (not text) type for JSON

Support for Icarus and Bitforce FPGAs was added

----------

API V1.5 was not released

----------

API V1.4 (Kano's interim release of cgminer v2.3.1)

Added API commands:
 'notify'

Modified API commands:
 'config' added "Device Code" and "OS"

Added "When" to the STATUS reply section of all commands

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
security, will return an "Access denied" Error Status

----------

API V1.1 (cgminer v2.2.4)

There were no changes to the API commands in cgminer v2.2.4,
however support was added to cgminer for IP address restrictions
with the --api-allow option

----------

API V1.1 (cgminer v2.2.2)

Prior to V1.1, devs/gpu incorrectly reported GPU0 Intensity for all GPUs

Modified API commands:
 'devs'/'gpu' added "Last Share Pool" and "Last Share Time" for each device

----------

API V1.0 (cgminer v2.2.0)

Remove default CPU support

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

miner.php is a PHP based interface to the cgminer RPC API
(referred to simply as the API below)

It can show rig details, summaries and input fields to allow you to change
cgminer
You can also create custom summary pages with it

It has two levels to the security:
1) cgminer can be configured to allow or disallow API access and access level
   security for miner.php
2) miner.php can be configured to allow or disallow privileged cgminer
   access, if cgminer is configured to allow privileged access for miner.php

---------

To use miner.php requires a web server with PHP

Basics: On xubuntu 11.04, to install apache2 and php, the commands are:
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

The basic cgminer option to enable the API is:

 --api-listen

or in your cgminer.conf

 "api-listen" : true,

(without the ',' on the end if it is the last item)

If the web server is running on the cgminer computer, the above
is the only change required to give miner.php basic access to
the cgminer API

-

If the web server runs on a different computer to cgminer,
you will also need to tell cgminer to allow the web server
to access cgminer's API and tell miner.php where cgminer is

Assuming a.b.c.d is the IP address of the web server, you
would add the following to cgminer:

 --api-listen --api-allow a.b.c.d

or in your cgminer.conf

 "api-listen" : true,
 "api-allow" : "a.b.c.d",

to tell cgminer to give the web server read access to the API

You also need to tell miner.php where cgminer is.
Assuming cgminer is at IP address e.f.g.h, then you would
edit miner.php and change the line

 $rigs = array('127.0.0.1:4028');

to

 $rigs = array('e.f.g.h:4028');

See --api-network or --api-allow for more access details
and how to give write access

You can however, also tell miner.php to find your cgminer rigs automatically
on the local subnet

Add the following to each cgminer:

 --api-mcast

or in your cgminer.conf

 "api-mcast" : true,

And in miner.php set $mcast = true;

This will ignore the value of $rigs and overwrite it with the list of zero or
more rigs found on the network in the timeout specified
A rig will not reply if the API settings would mean it would also ignore an
API request from the web server running miner.php

---------

Once you have a web server with PHP running

 copy your miner.php to the main web folder

On Xubuntu 11.04
 /var/www/

On Fedora 17
 /var/www/html/

On Windows
 see your windows Web/PHP documentation

Assuming the IP address of the web server is a.b.c.d
Then in your web browser go to:

 http://a.b.c.d/miner.php

Done :)

---------

The rest of this documentation deals with the more complex
functions of miner.php, using myminer.php, creaing custom
summaries and displaying multiple cgminer rigs

---------

If you create a file called myminer.php in the same web folder
where you put miner.php, miner.php will load it when it runs

This is useful, to put any changes you need to make to miner.php
instead of changing miner.php
Thus if you update/get a new miner.php, you won't lose the changes
you have made if you put all your changes in myminer.php
(and don't change miner.php at all)

A simple example myminer.php that defines 2 rigs
(that I will keep referring to further below) is:

<?php
#
$rigs = array('192.168.0.100:4028:A', '192.168.0.102:4028:B');
#
?>

Changes in myminer.php superscede what is in miner.php
However, this is only valid for variables in miner.php before the
2 lines where myminer.php is included by miner.php:

 if (file_exists('myminer.php'))
  include_once('myminer.php');
 
Every variable in miner.php above those 2 lines, can be changed by
simply defining them in your myminer.php

So although miner.php originally contains the line

 $rigs = array('127.0.0.1:4028');

if you created the example myminer.php given above, it would actually
change the value of $rigs that is used when miner.php is running
i.e. you don't have to remove or comment out the $rigs line in miner.php
It will be superceded by myminer.php

---------

The example myminer.php above also shows how to define more that one rig
to be shown my miner.php

Each rig string is 2 or 3 values seperated by colons ':'
They are simply an IP address or host name, followed by the
port number (usually 4028) and an optional Name string

miner.php displays rig buttons that will show the defails of a single
rig when you click on it - the button shows either the rig number,
or the 'Name' string if you provide it

PHP arrays contain each string seperated by a comma, but no comma after
the last one

So an example for 3 rigs would be:

 $rigs = array('192.168.0.100:4028:A', '192.168.0.102:4028:B',
               '192.168.0.110:4028:C');

Of course each of the rigs listed would also have to have the API
running and be set to allow the web server to access the API - as
explained before

---------

So basically, any variable explained below can be put in myminer.php
if you wanted to set it to something different to it's default value
and did not want to change miner.php itself every time you updated it

Below is each variable that can be changed and an explanation of each

---------

Default:
 $dfmt = 'H:i:s j-M-Y \U\T\CP';

Define the date format used to print full length dates
If you get the string 'UTCP' on the end of your dates shown, that
means you are using an older version of PHP and you can instead use:
 $dfmt = 'H:i:s j-M-Y \U\T\CO';

The PHP documentation on the date format is here:
 http://us.php.net/manual/en/function.date.php

---------

Default:
 $title = 'Mine';

Web page title
If you know PHP you can of course use code to define it e.g.
 $title = 'My Rig at: '.date($dfmt);

Which would set the web page title to something like:
 My Rig at: 10:34:00 22-Aug-2012 UTC+10:00

---------

Default:
 $readonly = false;

Set $readonly to true to force miner.php to be readonly
This means it won't allow you to change cgminer even if the cgminer API
options allow it to

If you set $readonly to false then it will check cgminer 'privileged'
and will show input fields and buttons on the single rig page
allowing you to change devices, pools and even quit or restart
cgminer

However, if the 'privileged' test fails, the code will set $readonly to
true

---------

Default:
 $userlist = null;

Define password checking and default access
 null means there is no password checking

$userlist is an array of 3 arrays e.g.
$userlist = array('sys' => array('boss' => 'bpass'),
                  'usr' => array('user' => 'upass', 'pleb' => 'ppass'),
                  'def' => array('Pools'));

'sys' is an array of system users and passwords (full access)
'usr' is an array of user level users and passwords (readonly access)
'def' is an array of custompages that anyone not logged in can view

Any of the 3 can be null, meaning there are none of that item

All validated 'usr' users are given $readonly = true; access
All validated 'sys' users are given the $readonly access you defined

If 'def' has one or more values, and allowcustompages is true, then
anyone without a password can see the list of custompage buttons given
in 'def' and will see the first one when they go to the web page, with
a login button at the top right

From the login page, if you login with no username or password, it will
show the first 'def' custompage (if there are any)

If you are logged in, it will show a logout button at the top right

---------

Default:
 $notify = true;

Set $notify to false to NOT attempt to display the notify command
table of data

Set $notify to true to attempt to display the notify command on
the single rig page
If your older version of cgminer returns an 'Invalid command'
coz it doesn't have notify - it just shows the error status table

---------

Default:
 $checklastshare = true;

Set $checklastshare to true to do the following checks:
If a device's last share is 12x expected ago then display as an error
If a device's last share is 8x expected ago then display as a warning
If either of the above is true, also display the whole line highlighted
This assumes shares are 1 difficulty shares

Set $checklastshare to false to not do the above checks

'expected' is calculated from the device MH/s value
So for example, a device that hashes at 380MH/s should (on average)
find a share every 11.3s
If the last share was found more than 11.3 x 12 seconds (135.6s) ago,
it is considered an error and highlighted
If the last share was found more than 11.3 x 8 seconds (90.4s) ago,
it is considered a warning and highlighted

The default highlighting is very subtle

---------

Default:
 $poolinputs = false;

Set $poolinputs to true to show the input fields for adding a pool
and changing the pool priorities on a single rig page
However, if $readonly is true, it will not display them

---------

Default:
 $rigs = array('127.0.0.1:4028');

Set $rigs to an array of your cgminer rigs that are running
 format: 'IP:Port' or 'Host:Port' or 'Host:Port:Name'
If you only have one rig, it will just show the detail of that rig
If you have more than one rig it will show a summary of all the rigs
 with buttons to show the details of each rig -
 the button contents will be 'Name' rather than rig number, if you
 specify 'Name'
e.g. $rigs = array('127.0.0.1:4028','myrig.com:4028:Sugoi');

---------

Default:
 $mcast = false;

Set $mcast to true to look for your rigs and ignore $rigs

---------

Default:
 $mcastexpect = 0;

The minimum number of rigs expected to be found when $mcast is true
If fewer are found, an error will be included at the top of the page

---------

Default:
 $mcastaddr = '224.0.0.75';

API Multicast address all cgminers are listening on

---------

Default:
 $mcastport = 4028;

API Multicast UDP port all cgminers are listening on

---------

Default:
 $mcastcode = 'FTW';

The code all cgminers expect in the Multicast message sent
The message sent is "cgm-code-listport"
Don't use the '-' character if you change it

---------

Default:
 $mcastlistport = 4027;

UDP port number that is added to the broadcast message sent
that specifies to the cgminers the port to reply on

---------

Default:
 $mcasttimeout = 1.5;

Set $mcasttimeout to the number of seconds (floating point)
to wait for replies to the Multicast message
N.B. the accuracy of the timing used to wait for the replies is
~0.1s so there's no point making it more than one decimal place

---------

Default:
 $mcastretries = 0;

Set $mcastretries to the number of times to retry the multicast

If $mcastexpect is 0, this is simply the number of extra times
that it will send the multicast request
N.B. cgminer doesn't listen for multicast requests for 1000ms after
each one it hears

If $mcastexpect is > 0, it will stop looking for replies once it
has found at least $mcastexpect rigs, but it only checks this rig
limit each time it reaches the $mcasttimeout limit, thus it can find
more than $mcastexpect rigs if more exist
It will send the multicast message up to $mcastretries extra times or
until it has found at least $mcastexpect rigs
However, when using $mcastretries, it is possible for it to sometimes
ignore some rigs on the network if $mcastexpect is less than the
number of rigs on the network and some rigs are too slow to reply

---------

Default:
 $allowgen = false;

Set $allowgen to true to allow customsummarypages to use 'gen' 
false means ignore any 'gen' options
This is disabled by default due to the possible security risk
of using it, see the end of this document for an explanation

---------

Default:
 $rigipsecurity = true;

Set $rigipsecurity to false to show the IP/Port of the rig
in the socket error messages and also show the full socket message

---------

Default:
 $rigtotals = true;
 $forcerigtotals = false;

Set $rigtotals to true to display totals on the single rig page
'false' means no totals (and ignores $forcerigtotals)

If $rigtotals is true, all data is also right aligned
With false, it's as before, left aligned

This option is just here to allow people to set it to false
if they prefer the old non-total display when viewing a single rig

Also, if there is only one line shown in any section, then no
total will be shown (to save screen space)
You can force it to always show rig totals on the single rig page,
even if there is only one line, by setting $forcerigtotals = true;

---------

Default:
 $socksndtimeoutsec = 10;
 $sockrcvtimeoutsec = 40;

The numbers are integer seconds

The defaults should be OK for most cases
However, the longer SND is, the longer you have to wait while
php hangs if the target cgminer isn't runnning or listening

RCV should only ever be relevant if cgminer has hung but the
API thread is still running, RCV would normally be >= SND

Feel free to increase SND if your network is very slow
or decrease RCV if that happens often to you

Also, on some windows PHP, apparently the $usec is ignored
(so usec can't be specified)

---------

Default:
 $hidefields = array();

List of fields NOT to be displayed
You can use this to hide data you don't want to see or don't want
shown on a public web page
The list of sections are:
 SUMMARY, POOL, PGA, GPU, NOTIFY, CONFIG, DEVDETAILS, DEVS
See the web page for the list of field names (the table headers)
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

Auto-refresh of the page (in seconds) - integers only

$ignorerefresh = true/false always ignore refresh parameters
$changerefresh = true/false show buttons to change the value
$autorefresh = default value, 0 means dont auto-refresh

---------

Default:
 $placebuttons = 'top';

Where to place the Refresh, Summary, Custom Pages, Quit, etc. buttons

Valid values are: 'top' 'bot' 'both'
 anything else means don't show them - case sensitive

---------

Default:
 $miner_font_family = 'verdana,arial,sans';
 $miner_font_size = '13pt';

Change these to set the font and font size used on the web page

---------

Default:
 $colouroverride = array();

Use this to change the web page colour scheme

See $colourtable in miner.php for the list of possible names to change

Simply put in $colouroverride, just the colours you wish to change

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
(or just completely ignore them and don't display the buttons)

---------

OK this part is more complex: Custom Summary Pages

A custom summary page in an array of 'section' => array('FieldA','FieldB'...)

The section defines what data you want in the summary table and the Fields
define what data you want shown from that section

Standard sections are:
 SUMMARY, POOL, PGA, GPU, NOTIFY, CONFIG, DEVDETAILS, DEVS, STATS, COIN

Fields are the names as shown on the headers on the normal pages

Fields can be 'name=new name' to display 'name' with a different heading
'new name'

There are also now joined sections:
 SUMMARY+POOL, SUMMARY+DEVS, SUMMARY+CONFIG, DEVS+NOTIFY, DEVS+DEVDETAILS
 SUMMARY+COIN

These sections are an SQL join of the two sections and the fields in them
are named section.field where section. is the section the field comes from
See the example further down

Also note:
- empty tables are not shown
- empty columns (e.g. an unknown field) are not shown
- missing field data shows as blank
- the field name '*' matches all fields except in joined sections
  (useful for STATS and COIN)

There are 2 hard coded sections:
 DATE - displays a date table like at the start of 'Summary'
 RIGS - displays a rig table like at the start of 'Summary'

Each custom summary requires a second array, that can be empty, listing fields
to be totaled for each section
If there is no matching total data, no total will show

---------

Looking at the Mobile example:

 $mobilepage = array(
  'DATE' => null,
  'RIGS' => null,
  'SUMMARY' => array('Elapsed', 'MHS av', 'Found Blocks=Blks', 
			Accepted', 'Rejected=Rej', 'Utility'),
  'DEVS+NOTIFY' => array('DEVS.Name=Name', 'DEVS.ID=ID', 'DEVS.Status=Status',
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

This will show 5 tables (according to $mobilepage)
Each table will have the chosen details for all the rigs specified in $rigs

 DATE
	A single box with the web server's current date and time

 RIGS
	A table of the rigs: description, time, versions etc

 SUMMARY

	This will use the API 'summary' command and show the selected fields:
		Elapsed, MHS av, Found Blocks, Accepted, Rejected and Utility
	However, 'Rejected=Rej' means that the header displayed for the 'Rejected'
	field will be 'Rej', instead of 'Rejected' (to save space)
	Same for 'Found Blocks=Blks' - to save space

 DEVS+NOTIFY

	This will list each of the devices on each rig and display the list of
	fields as shown
	It will also include the 'Last Not Well' field from the 'notify' command
	so you know when the device was last not well

	You will notice that you need to rename each field e.g. 'DEVS.Name=Name'
	since each field name in the join between DEVS and NOTIFY is actually
	section.fieldname, not just fieldname

	The join code automatically adds 2 fields to each GPU device: 'Name' and 'ID'
	They don't exist in the API 'devs' output but I can correctly calculate
	them from the GPU device data
	These two fields are used to join DEVS to NOTIFY i.e. find the NOTIFY
	record that has the same Name and ID as the DEVS record and join them

 POOL

	This will use the API 'pools' command and show the selected fields:
		POOL, Status, Accepted, Rejected, Last Share Time
	Again, I renamed the 'Rejected' field using 'Rejected=Rej', to save space

$mobilesum lists the sections and fields that should have a total
You can't define them for 'DATE' or 'RIGS' since they are hard coded tables
The example given:

 SUMMARY
	Show a total at the bottom of the columns for:
		MHS av, Found Blocks, Accepted, Rejected, Utility

	Firstly note that you use the original name i.e. for 'Rejected=Rej'
	you use 'Rejected', not 'Rej' and not 'Rejected=Rej'

	Secondly note that it simply adds up the fields
	If you ask for a total of a string field you will get the numerical
	sum of the string data

 DEVS+NOTIFY

	Simply note in this join example that you must use the original field
	names which are section.fieldname, not just fieldname

 POOL
	Show a total at the bottom of the columns for:
		Accepted and Rejected

	Again remember to use the original field name 'Rejected'

---------

With cgminer 2.10.2 and later, miner.php includes an extension to
the custom pages that allows you to apply SQL style commands to
the data: where, group, and having
cgminer 3.4.2 also includes another option 'gen'

As an example, miner.php includes a more complex custom page called 'Pools'
this includes the extension:

$poolsext = array(
 'POOL+STATS' => array(
        'where' => null,
        'group' => array('POOL.URL', 'POOL.Has Stratum',
                         'POOL.Stratum Active', 'POOL.Has GBT'),
        'calc' => array('POOL.Difficulty Accepted' => 'sum',
                        'POOL.Difficulty Rejected' => 'sum',
                        'STATS.Times Sent' => 'sum',
                        'STATS.Bytes Sent' => 'sum',
                        'STATS.Times Recv' => 'sum',
                        'STATS.Bytes Recv' => 'sum'),
        'gen' => array('AvShr', 'POOL.Difficulty Accepted/max(POOL.Accepted,1)),
        'having' => array(array('STATS.Bytes Recv', '>', 0)))
);

This allows you to group records together from one or more rigs
In the example, you'll get each Pool (with the same URL+Stratum+GBT settings)
listed once for all rigs and a sum of each of the fields listed in 'calc'


'where' and 'having' are an array of fields and restrictions to apply

In the above example, it will only display the rows where it contains the
'STATS.Bytes Recv' field with a value greater than zero
If the row doesn't have the field, it will always be included
All restrictions must be true in order for the row to be included
Any restiction that is invalid or unknown is true
An empty array, or null, means there are no restrictions

A restriction is formatted as: array('Field', 'restriction', 'value')
Field is the simple field name as normally displayed, or SECTION.Field
if it is a joined section (as in this case 'POOL+STATS')
The list of restrictions are:
'set' - true if the row contains the 'Field' ('value' is not required or used)
'=', '<', '<=', '>', '>' - a numerical comparison
'eq', 'lt', 'le', 'gt', 'ge' - a case insensitive string comparison

You can have multiple restrictions on a 'Field' - but all must be true to
include the row containing the 'Field'
e.g. a number range between 0 and 10 would be:
array('STATS.Bytes Recv', '>', 0), array('STATS.Bytes Recv', '<', 10)

The difference between 'where' and 'having' is that 'where' is applied to the
data before grouping it and 'having' is applied to the data after grouping it
- otherwise they work the same


'group' lists the fields to group over and 'calc' lists the function to apply
to other fields that are not part of 'group'

You can only see fields listed in 'group' and 'calc'

A 'calc' is formatted as: 'Field' => 'function'
The current list of operations available for 'calc' are:
'sum', 'avg', 'min', 'max', 'lo', 'hi', 'count', 'any'
The first 4 are as expected - the numerical sum, average, minimum or maximum
'lo' is the first string of the list, sorted ignoring case
'hi' is the last string of the list, sorted ignoring case
'count' is the number of rows in the section specified in the calc e.g.
 ('DEVS.Name' => 'count') would be the number of DEVS selected in the 'where'
 of course any valid 'DEVS.Xyz' would give the same 'count' value
'any' is effectively random: the field value in the 1st row of the grouped data
An unrecognised 'function' uses 'any'

A 'gen' allows you to generate new fields from any php valid function of any
of the other fields
 e.g. 'gen' => array('AvShr', 'POOL.Difficulty Accepted/max(POOL.Accepted,1)),
will generate a new field called GEN.AvShr that is the function shown, which
in this case is the average difficulty of each share submitted

THERE IS A SECURITY RISK WITH HOW GEN WORKS
It simply replaces all the variables with their values and then requests PHP
to execute the formula - thus if a field value returned from a cgminer API
request contained PHP code, it could be executed by your web server
Of course cgminer doesn't do this, but if you do not control the cgminer that
returns the data in the API calls, someone could modify cgminer to return a
PHP string in a field you use in 'gen'
Thus use 'gen' at your own risk
If someone feels the urge to write a mathematical interpreter in PHP to get
around this risk, feel free to write one and submit it to the API author for
consideration
