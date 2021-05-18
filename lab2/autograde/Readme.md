----------------------------------------
| Author: Kevin Chang (tc3149@nyu.edu) |
----------------------------------------

## give the script permission to run
~$ chmod +x autograde.sh

## run the script
~$ ./autograde.sh <your_sched_path> <num_of_test>
e.g.
~$ ./autograde.sh ../src/shed 100

v0.0.2
fix:
1. the script doesn't stop when scheduler produces wrong output
