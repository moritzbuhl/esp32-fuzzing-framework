#!/bin/sh

smallereq64=$(grep -h -o '"[^"]*"' $(find . -name '*.c') | while read l; do echo $((${#l} - 2)); done | sort -n | uniq -c | grep -B 100 ' 64$' | awk '{ printf "+"$1}' | cut -d+ -f2- | bc)
bigger64=$(grep -h -o '"[^"]*"' $(find . -name '*.c') | while read l; do echo $((${#l} - 2)); done | sort -n | uniq -c | grep -A 100 ' 65$' | awk '{ printf "+"$1}' | cut -d+ -f2- | bc)

echo "$bigger64 / $smallereq64 = $(echo $bigger64 / $smallereq64 | bc -l)"


