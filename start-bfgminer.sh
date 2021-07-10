#!/bin/sh
n="
"
startscreen() {
	name="$1"; shift
	cmd="$1"; shift
	if ! screen -ls | grep -q "^[[:space:]]\+[0-9]\+\.$name"; then
		screen -dmS "$name"
	else
		for i in 1 2 3; do
			screen -x "$name" -p 0 -X stuff $(echo 'x' | tr 'x' '\003')
		done
		screen -x "$name" -p 0 -X stuff "stty sane$n"
	fi
	screen -x "$name" -p 0 -X stuff "$cmd$n"
}
PROG=bfgminer
MYDIR="$(dirname "$0")"
WHICHPROG="$(which "$PROG" 2>/dev/null)"
if test -f "$MYDIR/$PROG" && test "$(realpath "$WHICHPROG" 2>/dev/null)" != "$(realpath "$MYDIR/$PROG")"; then
	PROG="cd $(realpath -s "$MYDIR")$n./$PROG"
fi
startscreen miner "${PROG} ${BFGMINER_OPTS}"
