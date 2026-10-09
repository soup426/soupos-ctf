#!/usr/bin/env bash
# menu-test.sh - menu names the commands the shell has, all of them (v0.60.54).
#
# Read from the source, both ways: the first word of every menu line must
# be a name dispatch() answers to, and every name dispatch() answers to
# must be on the menu, but for the ones kept off it on purpose (HIDDEN).
# Before v0.60.54 the menu still said jobs, clip, netinfo, ifconfig, arp,
# ping, nslookup and fg, none of which the shell knew any more, and left
# out some thirty that it did.
set -uo pipefail
cd "$(dirname "$0")/.."
HIDDEN="cat special"          # the CTF build's own, deliberately unlisted
fail=0
m=$(awk '/^static void cmd_menu\(void\) \{/,/^}/' src/shell.c | grep -o '{0, "    [a-z][a-z0-9_]*' | awk '{print $3}' | sort -u)
d=$(awk '/^static void dispatch\(const char \*buf\) \{/,/^}/' src/shell.c | grep -o '(p, "[a-z]*' | sed 's/(p, "//' | grep -v '^$' | sort -u)   # ([ is not a word: v0.60.79)
for h in $HIDDEN; do d=$(echo "$d" | grep -vx "$h"); done
missing=$(comm -23 <(echo "$m") <(echo "$d") | tr '\n' ' ')
unlisted=$(comm -13 <(echo "$m") <(echo "$d") | tr '\n' ' ')
[ -z "$missing" ] && echo "  ok    every command on the menu ($(echo "$m" | wc -l)) is one the shell runs" \
    || { echo "  FAIL  on the menu but not a command: $missing"; fail=1; }
[ -z "$unlisted" ] && echo "  ok    every command the shell runs ($(echo "$d" | wc -l), not counting: $HIDDEN) is on the menu" \
    || { echo "  FAIL  commands missing from the menu: $unlisted"; fail=1; }
# cmd_names (Tab, and inspect's builtins since v0.60.72) must name the same set
c=$(awk '/^static const char \*const cmd_names\[\] = \{/,/^};/' src/shell.c | grep -o '"[a-z]*"' | tr -d '"' | sort -u)
cmiss=$(comm -23 <(echo "$d") <(echo "$c") | tr '\n' ' ')
cextra=$(comm -13 <(echo "$d") <(echo "$c") | tr '\n' ' ')
[ -z "$cmiss$cextra" ] && echo "  ok    cmd_names (Tab, inspect) names the same $(echo "$c" | wc -l) commands" \
    || { echo "  FAIL  cmd_names lacks: $cmiss; has extra: $cextra"; fail=1; }
exit $fail
