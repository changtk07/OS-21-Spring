import os
from random import randrange

MAX_PROCESS = 128 #don't change this#
MAX_INTERVAL = 100
MAX_TOTAL_CPU = 200
MAX_CPU_BURST = 30
MAX_IO_BURST = 30

for n in range(0, 7):
  numProcess = randrange(MAX_PROCESS) + 1
  currTime = 0
  with open(f'input{n}', mode='w') as F:
    for i in range(numProcess):
      arriveTime = randrange(currTime, currTime+MAX_INTERVAL)
      totalCPU   = randrange(1, MAX_TOTAL_CPU)
      cpuBurst   = randrange(1, MAX_CPU_BURST)
      ioBurst    = randrange(1, MAX_IO_BURST)

      currTime = arriveTime
      str = f'{arriveTime}\t{totalCPU}\t{cpuBurst}\t{ioBurst}\n'
      F.write(str)
  F.close()
