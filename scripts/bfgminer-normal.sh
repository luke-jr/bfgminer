#!/bin/sh

MINER="amu"
OPTIONS=""
BFGMINER=`which bfgminer`

if [ -z "${BFGMINER}" ]
then
	echo "ERROR: Cannot find bfgminer in your path. PATH='${PATH}'"
	exit 1
elif [ -n "$1" ]
then
	MINER="$1"
fi

CONFIG="${HOME}/.bfgminer/bfgminer-${MINER}.conf"

if [ ! -f "${CONFIG}" ]
then
	echo "ERROR: Cannot find configuration at '${CONFIG}'."
	exit 1
else
	OPTIONS="${OPTIONS} --config ${CONFIG}"
fi

if [ "${MINER}" = "amu" ]
then
	# Only listen on amu, not others (fits to my environment again)
	OPTIONS=" --api-listen"
fi

${BFGMINER} --scan-serial ${MINER}:all ${OPTIONS}

exit 0
