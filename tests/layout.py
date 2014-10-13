#!/usr/bin/python
# -*- coding: utf-8 -*-

import sys
import os.path
import subprocess
import time
import re
import shlex
from display_array import *
from optparse import OptionParser

#_____________________________________________  
def compute_p(forward,prj):
  p = prj - forward / 2
  return p
#_____________________________________________  
def compute_one_prj(bsize,forward,inverse,prj):
  p = compute_p(forward,prj)
  q = 1
  psizes = abs(prj - forward / 2) * (inverse - 1) + (bsize / 8 / inverse - 1) + 1
  psizes = psizes + header + footer
  return psizes
#_____________________________________________  
def compute_one_layout(bsize,forward,inverse):
  psizes_max = int(0)
  psizes_sum = int(0)
    
  for prj in range(int(forward)):
    psizes = compute_one_prj(bsize,forward,inverse,prj)
    psizes_sum = psizes_sum + psizes	     
    if psizes > psizes_max:
      psizes_max = psizes

  redsum = float(psizes_sum*8);
  redsum = redsum / bsize;
  redmax = float(psizes_max*8*forward);
  redmax = redmax / bsize;	
  return redsum,redmax
#_____________________________________________  
def display_forward_inverse(forward,inverse):  

  print "inverse %d / forward %d"%(inverse,forward)  

  dis = display_array(2+loop)
  dis.set_column_on_right(1)
  dis.set_column_on_right(2)

  dis.new_line() 
  dis.set_column(1,'p')
  dis.set_column(2,'q')
  bs=int(base_block_size)  
  for i in range(1,loop+1):
    dis.set_column_on_right (i+2) 
    dis.set_column(i+2,'%d KB'%(int(bs)))
    bs=bs*2
  dis.separator()  

  for prj in range(forward):
  
    dis.new_line() 
    p = compute_p(forward,prj)
    dis.set_column(1,"%d"%(int(p))) 
    dis.set_column(2,'1') 
 
    bs=int(base_block_size)
    for i in range(1,loop+1):      
      size = compute_one_prj(bs*1024,forward,inverse,prj) 
      dis.set_column(2+i,'%d'%(size)) 
      bs=bs*2
          
  dis.display()
#_____________________________________________  
def syntax(string=None) :

  if string != None:
    print "!!! %s !!!\n"%(string)
  print "Usage:\n"
  print "./layout.py -F <nb faults>"
  print "./layout.py -i <inverse> -f <forward>"
  exit(-1)    
    

header=2
footer=1  
loop=7
base_block_size=1  
  
parser = OptionParser()
parser.add_option("-i","--inverse", action="store",type="string", dest="inverse", help="The inverse value.")
parser.add_option("-f","--forward", action="store",type="string", dest="forward", help="The forward value.")
parser.add_option("-F","--faults", action="store", type="string", dest="faults", help="The number of supported errors")

(options, args) = parser.parse_args()

print "\nHeader size %d bins / Footer size %d bins"%(header,footer)

  
  
if options.forward != None or options.inverse != None:
  if options.forward == None:
    syntax("Option -i requires -f")
  if options.inverse == None:
    syntax("Option -f requires -i")
  display_forward_inverse(int(options.forward),int(options.inverse))
  fault=int(options.forward)-int(options.inverse)
  inv_start=int(options.inverse)
  inv_stop=inv_start+1
  
elif options.faults == None:
  syntax()
else:      
  fault=int(options.faults)
  inv_start=fault
  inv_stop=32

dis = display_array(2+loop*2)
dis.new_line() 
dis.set_column(1,'%d'%(fault),'')
dis.set_column(2,'faults')

bs=int(base_block_size)  
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

for inverse in range(inv_start,inv_stop):
  forward=inverse+fault
  dis.new_line()     
  dis.set_column(1,"%d"%(inverse))
  dis.set_column(2,"%d"%(forward)) 
  bs=int(base_block_size)
  for i in range(1,loop+1):
    redsum,redmax = compute_one_layout(bs*1024,forward,inverse)
    dis.set_column(2*i+1,"%3.2f"%(redmax))
    dis.set_column(2*i+2,"%3.2f"%(redsum))        
    bs=2*bs
dis.display()
