# MINIX

Tasks modifying the kernel of Minix operating system.

## Negateexit

Add syscall ```int negateexit(int negate)``` that changes exit code of the process depending on the ```negate``` value. If ```negate``` has a value of ```1``` then the actual return code is negated, otherwise exit is handled the standard way. New child processes inherit the behaviour of their parents.

## Sched

Add new scheduling algorithm ```lowest unique bid``` and a syscall that allows processes to choose new scheduling strategy. The processes scheduled with ```lowest unique bid``` algorithm have a ```AUCTION_Q``` priority. These processes make a bid. The one with the lowest unique bid is chosen. If there is no such processes, the one from those with the highest bid is chosen.

## Hello queue

Add a driver of a queue ```/dev/hello_queue``` to which characters can be written and read from. Some other custom commands on the queue can be performed. The queue must be allocated dynamically.

