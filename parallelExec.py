#Source: https://stackoverflow.com/questions/8348978/how-can-i-run-a-program-hundreds-of-times-using-python
import subprocess
import time

bin_path = '/home/slint/Documents/Code/Developing_OPCUA/zkUAThrottle/zuthClient'
invocation_args = [[str(x*0.1)] for x in range(0,100)]

subprocs = []
for args in invocation_args:
    subprocs.append(subprocess.Popen([bin_path] + args))

while len(subprocs) > 0:
    subprocs = [p for p in subprocs if p.poll() is None]
#    time.sleep(0.05)

print 'Finished running all subprocs'
