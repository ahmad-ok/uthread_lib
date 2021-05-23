ahmadok, maherhanut
Ahmad Okosh (314925314), Maher Hanut (319029724)

FILES:
uthreads.cpp

REMARKS:
none

ANSWERS:
1- One good use of user-level threads is applying two different algorithms that try to solve one problem
   (especially ones that require heavy calculations), using user-level threads is good in this case since
   we have low overhead, and it is easier and faster than kernel-level threads, therefore we have better
   processing power
   
2- Advantages:
   Google Chromes creates a process for each tab for security purposes, because if one gets compromised, 
   it does not affect the security of the other tabs 
   Disadvantages:
   Since we're creating a process for each tab we're giving each tab a memory space, therefore we're wasting
   a lot of memory. And in addition we have high overhead
   
3- a) keyboard sends keyboard interrupt (software or hardware) to shell
   b) Shell sends the interrupt for the OS to handle
   c) The OS sends the signal to the program, after which the program handles the signal as appropriate

4- Real Time: How long a process takes time in real life (for example real seconds, millisecond)
   Virtual Time: Time that the program was in running state
