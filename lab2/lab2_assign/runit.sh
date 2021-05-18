#!/bin/bash

# Author: Hubertus Franke  (frankeh@cims.nyu.edu)
OUTDIR=${1:-.}
shift
SCHED=${*:-../src/sched}
echo "sched=<$SCHED> outdir=<$OUTDIR>"

# if you want -v output  run with ...  ./runit.sh youroutputdir   sched -v 

RFILE=./rfile
INS="0 1 2 3 4 5 6"

#SCHEDS="F L S R2 R5 P2 P5 E2 E4"
SCHEDS="  F    L    S   R2    R5    P2   P5:3  E2:5 E4"  

for f in ${INS}; do
	for s in ${SCHEDS}; do 
		echo "${SCHED} ${SCHEDARGS} -s${s} input${f} ${RFILE}"
		${SCHED} -s${s} input${f} ${RFILE} > ${OUTDIR}/out_${f}_${s}
	done
done

