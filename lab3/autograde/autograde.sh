#!/bin/bash

# Author: Kevin Chang (tc3149@nyu.edu)

MAX_PROC=9
MAX_VMA=8
MAX_INSTR=10000
MAX_HOLES=7

num_procs=$(shuf -i1-${MAX_PROC} -n1)
num_vmas=$(shuf -i1-${MAX_VMA} -n1)
num_instrs=$(shuf -i100-${MAX_INSTR} -n1)
num_exits=$(shuf -i0-${num_procs} -n1)
num_holes=$(shuf -i0-${MAX_HOLES} -n1)
perc=$(shuf -i0-100 -n 1)
lambda=$(seq 0 .01 5 | shuf | head -n1 )

#echo "num_procs = ${num_procs}"
#echo "num_vmas = ${num_vmas}"
#echo "num_instrs = ${num_instrs}"
#echo "num_exits = ${num_exits}"
#echo "num_holes = ${num_holes}"
#echo "perc = ${perc}"
#echo "lambda = ${lambda}"

num_tests=$1
mmu=$2
mmu_generator=/home/frankeh/Public/mmu_generator
ref_mmu=/home/frankeh/Public/mmu

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo -ne "${GREEN}test 0/${num_tests} passed (0%)${NC}"

for i in $(seq 1 $num_tests); do {
	if [ ! -d inputs ]; then {
		mkdir inputs
	} fi
	for j in $(seq 1 1); do {
		${mmu_generator} -P${num_procs} -V${num_vmas} -i${num_instrs} -E${num_exits} -H${num_holes} -r${perc} -L${lambda} -mwv > ./inputs/in${j}
	} done
	if [ ! -d out ]; then {
		mkdir out
	} fi
	if [ ! -d refout ]; then {
		mkdir refout
	} fi
	./scripts/runit.sh ./inputs ./out ${mmu} > .tmp
	./scripts/runit.sh ./inputs ./refout ${ref_mmu} > .tmp
	./scripts/gradeit.sh ./refout ./out > .tmp
	
	progress=$(echo ${i} ${num_tests} | awk '{ printf "%.2f", $1 / $2 * 100 }')
	line=$(tail -1 .tmp)
	IFS_cpy=${IFS}
	IFS=' '
	read -a tokens <<< "$line"
	if [[ "${tokens[1]}" != "3" || "${tokens[2]}" != "3" || "${tokens[3]}" != "3" || "${tokens[4]}" != "3" || "${tokens[5]}" != "3" || "${tokens[6]}" != "3" ]]; then {
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

echo ""

if [ ! err ]; then {
	echo "${GREEN}test ${num_tests}/${num_tests} passed (100%)${NC}"
} fi

rm -f .tmp
tput bel

