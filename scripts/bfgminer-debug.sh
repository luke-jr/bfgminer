#!/bin/sh

MINER="amu"
BFGMINER=`which bfgminer`
OPTIONS=""

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
	# Only listen on AMU (fits to my environment again
	OPTIONS=" --api-listen"
fi

${BFGMINER} --scan-serial ${MINER}:all ${OPTIONS} --debug -T 2> ${HOME}/.bfgminer/debug-${MINER}.log

exit 0
