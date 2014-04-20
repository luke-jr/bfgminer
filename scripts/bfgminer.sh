#!/bin/sh

if [ "$1" = "d" ]
then
	screen -dmS bfgminer-amu /opt/bin/bfgminer-debug.sh amu
	screen -dmS bfgminer-bes /opt/bin/bfgminer-debug.sh bes
else
	screen -dmS bfgminer-amu /opt/bin/bfgminer-normal.sh amu
	screen -dmS bfgminer-bes /opt/bin/bfgminer-normal.sh bes
fi

if [ "$1" != "c" ]
then
	screen -r bfgminer-amu
fi
