#!/bin/bash

# Author: Kevin Chang (tc3149@nyu.edu)
TMP=./.tmp
FSCHED=/home/frankeh/Public/sched
SCHED=$1
N=$2

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

for i in $(seq 1 $N); do
	python3 ./autogen.py
	./runit.sh out $SCHED > $TMP
	./runit.sh refout $FSCHED > $TMP
	./gradeit.sh refout out > $TMP
        line=$(tail -1 $TMP)
	IFS=' '
	read -a tokens <<< "$line"
	if [ ${tokens[0]} = ${tokens[3]} ]; then {
		echo -e "${YELLOW}$i\t${GREEN}$line${NC}"
	}
	else {
		echo -e "${YELLOW}$i\t${RED}$line${NC}"
		echo -e "\n$(cat $TMP)"
		break
	}
	fi
done

rm -rf $TMP
tput bel
