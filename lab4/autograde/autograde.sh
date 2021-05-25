#!/bin/bash

# Author: Kevin Chang (tc3149@nyu.edu)

MAX_TRACKS=2000
MAX_IOS=10000

num_tracks=$(shuf -i 1-${MAX_TRACKS} -n 1)
num_ios=$(shuf -i 1-${MAX_IOS} -n 1)
lambda=$(seq .1 .1 10. | shuf | head -n 1)
factor=$(seq .5 .1 1.5 | shuf | head -n 1)

num_tests=$1
iosched=$2
iomake=~frankeh/Public/iomake
ref=~frankeh/Public/iosched

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

if [ ! -d out ]; then {
	mkdir out
} fi
if [ ! -d refout ]; then {
	mkdir refout
} fi

echo -ne "${GREEN}test 0/${num_tests} passed (0%)${NC}"

for i in $(seq 1 ${num_tests}); do {
	> .tmp

	for j in $(seq 0 9); do {
		num_tracks=$(shuf -i 8-${MAX_TRACKS} -n 1)
		num_ios=$(shuf -i 1-${MAX_IOS} -n 1)
		lambda=$(seq .1 .1 10. | shuf | head -n 1)
		factor=$(seq .5 .1 1.5 | shuf | head -n 1)
		
		echo "${iomake} -v -t ${num_tracks} -i ${num_ios} -L ${lambda} -f ${factor} > input${j}" >> .tmp

		${iomake} -v -t ${num_tracks} -i ${num_ios} -L ${lambda} -f ${factor} > input${j}
	} done

	./runit.sh out ${iosched} >> .tmp
	./runit.sh refout ${ref} >> .tmp
	./gradeit.sh refout out >> .tmp

	progress=$(echo ${i} ${num_tests} | awk '{ printf "%.2f", $1 / $2 * 100 }')
	line=$(tail -1 .tmp)
	IFS_cpy=${IFS}
	IFS=' '
	read -a tokens <<< "$line"
	if [ ${tokens[1]} != ${tokens[4]} ]; then {
		let err=true
		echo -e "\r${RED}test ${i}/${num_tests} failed (${progress}%)${NC}"
		echo "$(<.tmp)"
		break
	}
	else {
		echo -ne "\r${GREEN}test ${i}/${num_tests} passed (${progress}%)${NC}"
	} fi
	IFS=${IFS_cpy}
} done

echo $err

if [ ! err ]; then {
	echo "${GREEN}test ${num_tests}/${num_tests} passed (100%)${NC}"
} fi

rm -f .tmp
tput bel

