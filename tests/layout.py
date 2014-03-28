#!/usr/bin/python
# -*- coding: utf-8 -*-

import sys
import os.path
import subprocess
import time
import re
import shlex
from display_array import *



def compute_one_layout(block,forward,inverse):
  psizes_max = int(0)
  psizes_sum = int(0)

  for prj in range(int(forward)):
    p = prj - forward / 2
    q = 1
    bsize = int(block) * 1024
    psizes = abs(prj - forward / 2) * (inverse - 1) + (bsize / 8 / inverse - 1) + 1
    # Add header 2 + footer 1
    psizes = psizes + header + footer
    psizes_sum = psizes_sum + psizes	     
    if psizes > psizes_max:
      psizes_max = psizes

  redsum = float(psizes_sum*8);
  redsum = redsum / bsize;
  redmax = float(psizes_max*8*forward);
  redmax = redmax / bsize;	
  return redsum,redmax
  
  
if len(sys.argv) < 2:
  print "Usage ./layout.py <nb faults>"
  exit(-1)
  
header=2
footer=1  
loop=7 
fault=int(sys.argv[1])

print "\nHeader size %d bins / Footer size %d bins"%(header,footer)
dis = display_array(2+loop*2)
dis.new_line() 
dis.set_column(1,'%d'%(fault),'')
dis.set_column(2,'faults')

bs=int(4)  
for i in range(1,loop+1):
  dis.set_column(2*i+1,'%d'%(int(bs)),'')
  dis.set_column(2*i+2,'KB')
  bs=bs*2
dis.separator()   

dis.new_line()       
dis.set_column(1,'Inv')
dis.set_column(2,'Fwd')
for i in range(1,loop+1):
  dis.set_column(2*i+1,'Max')
  dis.set_column(2*i+2,'Sum')

dis.separator()   

for inverse in range(fault,31):
  forward=inverse+fault
  dis.new_line()     
  dis.set_column(1,"%d"%(inverse))
  dis.set_column(2,"%d"%(forward)) 
  bs=4 
  for i in range(1,loop+1):
    redsum,redmax = compute_one_layout(bs,forward,inverse)
    dis.set_column(2*i+1,"%3.2f"%(redmax))
    dis.set_column(2*i+2,"%3.2f"%(redsum))        
    bs=2*bs
dis.display()
