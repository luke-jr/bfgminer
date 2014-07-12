#!/bin/sh
gitdesc=
if [ -e .git ]; then
	# Some versions of git require `git diff` to scan and update dirty-or-not status
	git diff >/dev/null 2>/dev/null
	
	gitdesc=$(git describe)
fi
if [ -z "$gitdesc" ]; then
	current=$(sed 's/^\#define[[:space:]]\+BFG_GIT_DESCRIBE[[:space:]]\+\"\(.*\)\"$/\1/;t;d' version.h)
	if [ -z "$current" ]; then
		gitdesc='"PACKAGE_VERSION"-unknown'
	else
		gitdesc="$current"
	fi
fi
version=$(echo "$gitdesc" | sed 's/^bfgminer-//')
cat <<EOF
#define BFG_GIT_DESCRIBE "$gitdesc"
#ifdef VERSION
#  undef VERSION
#endif
#define VERSION "$version"
EOF
