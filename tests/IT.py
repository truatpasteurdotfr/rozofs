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

fileSize=int(4)
loop=int(32)
process=int(8)
NB_SID=int(8)
STC_SID=int(8)
nbGruyere=int(1000)
stopOnFailure=False
fuseTrace=False
DEFAULT_MNT="mnt1_1_g0"
mnt=DEFAULT_MNT
DEFAULT_RETRIES=int(20)

 

#___________________________________________________
def my_duration (val):
#___________________________________________________

  hour=val/3600  
  min=val%3600  
  sec=min%60
  min=min/60
  return "%2d:%2.2d:%2.2d"%(hour,min,sec)

#___________________________________________________
def reset_counters():
# Use debug interface to reset profilers and some counters
#___________________________________________________
  return

  string='./dbg.sh all profiler reset'
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  string='./dbg.sh io diskThreads reset'
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  if fuseTrace == True:

    string='./dbg.sh fs1 fuse reset'
    parsed = shlex.split(string)
    cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    string='./dbg.sh fs1 trc_fuse count 256'
    parsed = shlex.split(string)
    cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    string='./dbg.sh fs1 trc_fuse enable'
    parsed = shlex.split(string)
    cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  else:

    string='./dbg.sh fs1 trc_fuse disable'
    parsed = shlex.split(string)
    cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)  
#___________________________________________________
def get_device_numbers(hid):
# Use debug interface to get the number of sid from exportd
#___________________________________________________
  device_number=1
  mapper_modulo=1
  mapper_redundancy=1 
  
  string="./build/src/rozodiag/rozodiag -i localhost%d -p 50028 -c device device"%(hid+1)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  for line in cmd.stdout:
    if "device_number" in line:
      device_number=line.split()[2]
    if "mapper_modulo" in line:
      mapper_modulo=line.split()[2]
    if "mapper_redundancy" in line:
      mapper_redundancy=line.split()[2]
      
  return device_number,mapper_modulo,mapper_redundancy   
  

#___________________________________________________
def get_sid_nb():
# Use debug interface to get the number of sid from exportd
#___________________________________________________

  sid=int(0)
  
  string="./build/src/rozodiag/rozodiag -p 50000 -c vfstat_stor"
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


  for line in cmd.stdout:
    if "UP" in line or "DOWN" in line:
      sid=sid+1
      
  per_site=int(sid)      
  p = subprocess.Popen(["ps","-ef"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  for proc in p.stdout:
    if "-o geocli" in proc:
      per_site=per_site/2
      break
  return sid,per_site    

#___________________________________________________
def export_count_sid_up ():
# Use debug interface to count the number of sid up 
# seen from the export. 
#___________________________________________________

  string="./build/src/rozodiag/rozodiag -p 50000 -c vfstat_stor"
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  match=int(0)
  for line in cmd.stdout:
    if "UP" in line:
      match=match+1
    
  return match
#___________________________________________________
def get_rozofmount_port ():

  p = subprocess.Popen(["ps","-ef"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  for proc in p.stdout:
    if not "rozofsmount/rozofsmount" in proc:
      continue
      
    for words in proc.split():
      if mnt in words.split("/"):        
	for opt in proc.split(" "):
	  if opt.startswith("instance="):
            instance=opt.split("instance=")[1]
	    port = (int(instance)*3+50003)
	    return int(port)
	    
  return int(0)
#___________________________________________________
def get_site_number ():

  port=get_rozofmount_port()

  string="./build/src/rozodiag/rozodiag -p %d -c start_config"%(port)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  
  for line in cmd.stdout:
    words=line.split('=')
    if words[0].strip() == "running_site":    
      return int(words[1])
  return 0  
#___________________________________________________
def get_storcli_port ():
  p = subprocess.Popen(["ps","-ef"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  for proc in p.stdout:
    if not "%s "%(mnt) in proc:
      continue
    if "rozolauncher" in proc:
      continue
    if not " -o rozofsmount" in proc:
      continue      
    if not "storcli -i 1 -H " in proc:
      continue
      
    next=0
    for word in proc.split(" "):
      if word == "-D":
        next=1  
      else:
        if next == 1:
	  return word
  return 0	  
#___________________________________________________
def storcli_count_sid_available ():
# Use debug interface to count the number of sid 
# available seen from the storcli. 
#___________________________________________________

  storcli_port=get_storcli_port()  	      
  string="./build/src/rozodiag/rozodiag -p %s -c storaged_status"%(storcli_port)       
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  # Looking for state=UP and selectable=YES
  match=int(0)
  for line in cmd.stdout:
    words=line.split('|')
    if len(words) >= 11:
      if 'YES' in words[6] and 'UP' in words[4]:
        match=match+1
    
  return match  

#___________________________________________________
def loop_wait_until (success,retries,function):
# Loop until <function> returns <success> for a maximum 
# of <retries> attempt (one attempt per second)
#___________________________________________________

  up=int(0)

  while int(up) != int(success):

    retries=retries-1
    if retries == 0:
      print "Maximum retries reached. %s is %d"%(function,up)      
      return False
      
    sys.stdout.write(".")
    sys.stdout.flush()     
     
    up=getattr(sys.modules[__name__],function)()
    time.sleep(1)
    
  time.sleep(1)    
  return True  
    
#___________________________________________________
def wait_until_all_sid_up (retries=DEFAULT_RETRIES) :
# Wait for all sid up seen by storcli as well as export
#___________________________________________________

  if loop_wait_until(STC_SID,retries,'storcli_count_sid_available') == False:
    return False
  if loop_wait_until(NB_SID,retries,'export_count_sid_up') == False:
    return False
  return True  
  
    
#___________________________________________________
def wait_until_one_sid_down (retries=DEFAULT_RETRIES) :
# Wait until one sid down seen by storcli 
#___________________________________________________

  if loop_wait_until(STC_SID-1,retries,'storcli_count_sid_available') == False:
    return False
  return True   
#___________________________________________________
def storageStart (hid,count=int(1)) :

  while count != 0:
  
    sys.stdout.write("\rStorage %d restart"%(hid+1)) 
    sys.stdout.flush()
        
    os.system("./setup.sh storage %s start"%(hid+1))    

    if wait_until_all_sid_up() == True:
      return 0
    count=count-1
        
  return 1 
#___________________________________________________
def storageStop (hid) :
  
  sys.stdout.write("\r                                 ")
  sys.stdout.flush()  
  sys.stdout.write("\rStorage %d stop"%(hid+1))
  sys.stdout.flush()

  os.system("./setup.sh storage %s stop"%(hid+1))
  wait_until_one_sid_down()   
    
#___________________________________________________
def storageFailed (test) :
# Run test names <test> implemented in function <test>()
# under the circumstance that a storage is stopped
#___________________________________________________

  if wait_until_all_sid_up() == False:
    return 1

  for sid in range(STC_SID):

    hid=sid+(site*STC_SID)
    
    storageStop(hid)
    reset_counters()
    
    try:
      # Resolve and call <test> function
      ret = getattr(sys.modules[__name__],test)()         
    except:
      ret = 1
      
    if ret != 0:
      return 1
      
    ret = storageStart(hid)  

      
  return 0

#___________________________________________________
def snipper_storcli ():
# sub process that periodicaly resets the storcli(s)
#___________________________________________________
  
  while True:

      sys.stdout.write("\r                                 ")
      sys.stdout.flush()  
      sys.stdout.write("\rStorcli reset")
      sys.stdout.flush()

      p = subprocess.Popen(["ps","-ef"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
      for proc in p.stdout:
        if not "storcli -i" in proc:
          continue
        if "rozolauncher" in proc:
          continue
        if not "%s"%(mnt) in proc:
          continue  
	
        pid=proc.split()[1]
        os.system("kill -9 %s"%(pid))
	    

      for i in range(9):
        sys.stdout.write(".")
        sys.stdout.flush()
        time.sleep(1)

#___________________________________________________
def storcliReset (test):
# Run test names <test> implemented in function <test>()
# under the circumstance where storcli is periodicaly reset
#___________________________________________________

  global loop

  time.sleep(3)
 
  # Start process that reset the storages
  string="./IT.py --snipper storcli --mount %s"%(mnt)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stderr=subprocess.PIPE)

  saveloop=loop
  loop=loop*2
  
  try:
    # Resolve and call <test> function
    ret = getattr(sys.modules[__name__],test)()         
  except:
    ret = 1
    
  loop=saveloop

  # kill the storcli snipper process
  cmd.kill()

  if ret != 0:
      return 1
  return 0

#___________________________________________________
def snipper_storio ():
# sub process that periodicaly resets the storio(s)
#___________________________________________________

  while True:
    for sid in range(STC_SID):      

      if wait_until_all_sid_up() == False:
        return 1
 
      hid=sid+(site*STC_SID)
      
      sys.stdout.write("\r                                 ")
      sys.stdout.flush()
      sys.stdout.write("\rStorio %d reset"%(hid+1))
      sys.stdout.flush()

      os.system("./setup.sh storio %d reset"%(hid+1))


#___________________________________________________
def storageReset (test):
# Run test names <test> implemented in function <test>()
# under the circumstance that storio(s) are periodicaly reset
#___________________________________________________
  global loop
  
 
  # Start process that reset the storages
  string="./IT.py --snipper storio --mount %s"%(mnt)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stderr=subprocess.PIPE)

  saveloop=loop
  loop=loop*5
  
  try:
    # Resolve and call <test> function
    ret = getattr(sys.modules[__name__],test)()         
  except:
    ret = 1
    
  loop=saveloop

  # kill the storio snipper process
  cmd.kill()

  if ret != 0:
      return 1
  return 0

#___________________________________________________
def snipper (target):
# A snipper command has been received for a given target. 
# Resolve function snipper_<target> and call it.
#___________________________________________________

  func='snipper_%s'%(target)
  try:
    ret = getattr(sys.modules[__name__],func)()         
  except:
    print "No such snipper %s"%(func)
    ret = 1
  return ret  

#___________________________________________________
def wr_rd_total ():
#___________________________________________________

  do_compile_program('./rw')
  ret=os.system("./rw -process %d -loop %d -fileSize %d -total -mount %s"%(process,loop,fileSize,mnt))
  return ret  

#___________________________________________________
def wr_rd_partial ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -partial -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_rd_random ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -random -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_rd_total_close ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -total -closeAfter -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_rd_partial_close ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -partial -closeAfter -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_rd_random_close ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -random -closeAfter -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_close_rd_total ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -total -closeBetween -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_close_rd_partial ():
#___________________________________________________

  do_compile_program('./rw')
  ret=os.system("./rw -process %d -loop %d -fileSize %d -partial -closeBetween -mount %s"%(process,loop,fileSize,mnt))
  return ret 

#___________________________________________________
def wr_close_rd_random ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -random -closeBetween -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_close_rd_total_close ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -total -closeBetween -closeAfter -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_close_rd_partial_close ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -partial -closeBetween -closeAfter -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def wr_close_rd_random_close ():
#___________________________________________________

  do_compile_program('./rw')
  return os.system("./rw -process %d -loop %d -fileSize %d -random -closeBetween -closeAfter -mount %s"%(process,loop,fileSize,mnt))

#___________________________________________________
def rw2 ():
#___________________________________________________

  do_compile_program('./rw2')
  return os.system("./rw2 -loop %s -file %s/ze_rw2_test_file"%(loop,mnt))

#___________________________________________________
def prepare_file_to_read(filename,mega):
#___________________________________________________

  if not os.path.exists(filename):
    os.system("dd if=/dev/zero of=%s bs=1M count=%s 1> /dev/null"%(filename,mega))    

#___________________________________________________
def read_parallel ():
#___________________________________________________

  do_compile_program('./read_parallel')
  zefile='%s/myfile'%(mnt)
  prepare_file_to_read(zefile,fileSize) 
  ret=os.system("./read_parallel -process %s -loop %s -file %s"%(process,loop,zefile)) 
  return ret   

#___________________________________________________
def xattr():
#___________________________________________________

  do_compile_program('./test_xattr')  
  return os.system("./test_xattr -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def link():
#___________________________________________________

  do_compile_program('./test_link')  
  return os.system("./test_link -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def readdir():
#___________________________________________________

  do_compile_program('./test_readdir')  
  return os.system("./test_readdir -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def rename():
#___________________________________________________

  do_compile_program('./test_rename')  
  ret=os.system("./test_rename -process %d -loop %d -mount %s"%(process,loop,mnt))
  return ret 

#___________________________________________________
def chmod():
#___________________________________________________

  do_compile_program('./test_chmod')  
  return os.system("./test_chmod -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def truncate():
#___________________________________________________

  do_compile_program('./test_trunc')  
  return os.system("./test_trunc -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def lock_posix_passing():
#___________________________________________________

  do_compile_program('./test_file_lock')  
  zefile='%s/lock'%(mnt)
  try:
    os.remove(zefile)
  except:
    pass  
  return os.system("./test_file_lock -process %d -loop %d -file %s -nonBlocking"%(process,loop,zefile))  

#___________________________________________________
def lock_posix_blocking():
#___________________________________________________

  do_compile_program('./test_file_lock')  
  zefile='%s/lock'%(mnt)
  try:
    os.remove(zefile)
  except:
    pass  
  ret=os.system("./test_file_lock -process %d -loop %d -file %s"%(process,loop,zefile))
  return ret 

#___________________________________________________
def lock_bsd_passing():
#___________________________________________________

  do_compile_program('./test_file_lock')  
  zefile='%s/lock'%(mnt)
  try:
    os.remove(zefile)
  except:
    pass  
  return os.system("./test_file_lock -process %d -loop %d -file %s -nonBlocking -bsd"%(process,loop,zefile))


#___________________________________________________
def quiet():
#___________________________________________________

  while True:
    time.sleep(60)


#___________________________________________________
def lock_bsd_blocking():
#___________________________________________________

  do_compile_program('./test_file_lock')  
  zefile='%s/lock'%(mnt)
  try:
    os.remove(zefile)
  except:
    pass  
  return os.system("./test_file_lock -process %d -loop %d -file %s -nonBlocking"%(process,loop,zefile))  

#___________________________________________________
def gruyere_one_reread():
# reread files create by test_rebuild utility to check
# their content
#___________________________________________________

  do_compile_program('./test_rebuild')  
  return os.system("./test_rebuild -action check -nbfiles %d -mount %s"%(int(nbGruyere),mnt))

#___________________________________________________
def gruyere_reread():
# reread files create by test_rebuild with every storage
# possible fault to check every projection combination
#___________________________________________________

  ret = gruyere_one_reread()
  if ret != 0:
    return ret
  return storageFailed('gruyere_one_reread')

#___________________________________________________
def gruyere_write():
# Use test_rebuild utility to create a bunch of files
#___________________________________________________

  do_compile_program('./test_rebuild')  
  return os.system("./test_rebuild -action create -nbfiles %d -mount %s"%(int(nbGruyere),mnt))  

#___________________________________________________
def gruyere():
# call gruyere_write that create a bunch of files while
# block per block while storages are reset. This makes
# files with block dispersed on every storage. 
#___________________________________________________

  ret = storageReset('gruyere_write')
  if ret != 0:
    return ret
  return gruyere_reread()  

#___________________________________________________
def rebuild_one() :
# test rebuilding device per device
#___________________________________________________

  ret=1 
  for sid in range(STC_SID):

    hid=sid+(site*STC_SID)
    
    device_number,mapper_modulo,mapper_redundancy = get_device_numbers(hid)
    
    dev=hid%int(mapper_modulo)
    os.system("./setup.sh storage %d device-delete %d 1> /dev/null"%(hid+1,dev))
    ret = os.system("./setup.sh storage %d device-rebuild %d -g %s 1> /dev/null"%(hid+1,dev, site))
    if ret != 0:
      return ret
      
    if int(mapper_modulo) > 1:
      dev=(dev+2)%int(mapper_modulo)
      os.system("./setup.sh storage %d device-delete %d 1> /dev/null"%(hid+1,dev))
      ret = os.system("./setup.sh storage %d device-rebuild %d -g %s 1> /dev/null"%(hid+1,dev, site))
      if ret != 0:
	return ret
            
    ret = gruyere_one_reread()  
    if ret != 0:
      return ret 
      
  ret = gruyere_reread()          
  return ret

#___________________________________________________
def rebuild_all() :
# test re-building a whole storage
#___________________________________________________

  ret=1 
  for sid in range(STC_SID):

    hid=sid+(site*STC_SID)

    os.system("./setup.sh storage %d device-delete all  1> /dev/null"%(hid+1))
    ret = os.system("./setup.sh storage %d device-rebuild all -g %s 1> /dev/null"%(hid+1,site))
    if ret != 0:
      return ret

    ret = gruyere_one_reread()  
    if ret != 0:
      return ret    
  ret = gruyere_reread()         
  return ret
#___________________________________________________
def rebuild_fid() :
# test rebuilding per FID
#___________________________________________________

  for f in range(int(nbGruyere)/10):

    # Get the split of file on storages      
    string="./setup.sh cou %s/rebuild/%d"%(mnt,f+1)
    parsed = shlex.split(string)
    cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # loop on the bins file constituting this file, and ask
    # the storages for a rebuild of the file
    bins_list = []
    for line in cmd.stdout:
	  
      if "FID" in line:
        words=line.split();
	if len(words) >= 2:
          fid=words[2]
	  continue
	  	    
      if "/bins_" in line:
        bins_list.append(line)
	continue	  

    # loop on the bins file constituting this file, and ask
    # the storages for a rebuild of the file
    for line in bins_list:
        words=line.split();
	if len(words) >= 2:
	
	  name=words[1].split('/')
	  check=name[len(name)-2]
	  if check == "bins_0" or check == "bins_1": 
	    cidsid=name[len(name)-4].split("storage_")[1].split('-')	  
          else:
	    cidsid=name[len(name)-5].split("storage_")[1].split('-')	  

          hid=int(cidsid[1])+(int(site)*int(STC_SID))
	  
          string="./setup.sh storage %s fid-rebuild -g %s -s %s/%s -f %s "%(hid,site,cidsid[0],cidsid[1],fid)
          parsed = shlex.split(string)
          cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
          cmd.wait()
	  
          if cmd.returncode != 0:
            return 1        

  return gruyere_one_reread()  

#___________________________________________________
def append_circumstance_test_list(list,input_list,circumstance):
# Add to <list> the list <input_list> prefixed with 
# <circumstance> that should be a valid circumstance test list.
# function <circumstance>() should exist to implement this
# particuler test circumstance.
#___________________________________________________

   for tst in input_list:
     list.append("%s/%s"%(circumstance,tst)) 

#___________________________________________________
def do_compile_program(program): 
# compile program if program.c is younger
#___________________________________________________

  if not os.path.exists(program) or os.stat(program).st_mtime < os.stat("%s.c"%(program)).st_mtime:
    os.system("gcc -g %s.c -lpthread -o %s"%(program,program))
     
#___________________________________________________
def do_run_list(list):
# run a list of test
#___________________________________________________

  tst_num=int(0)
  failed=int(0)
  success=int(0)
  
  dis = display_array(4)
  dis.new_line()  
  dis.set_column(1,'#')
  dis.set_column_on_right(1)  
  dis.set_column(2,'Name')
  dis.set_column(3,'Result')
  dis.set_column(4,'Duration')
  dis.set_column_on_right(4)  
  dis.separator()   

  time_start=time.time()
  
  total_tst=len(list)    
  for tst in list:
  
    tst_num=tst_num+1
    
    sys.stdout.write( "\r___%4d/%d : %s\n"%(tst_num,total_tst,tst))

    dis.new_line()  
    dis.set_column(1,'%s'%(tst_num))
    dis.set_column(2,tst)

    
    # Split optional circumstance and test name
    split=tst.split('/') 
    
    time_before=time.time()
    reset_counters()    
    
    if len(split) > 1:
    
      # There is a test circumstance. resolve and call the circumstance  
      # function giving it the test name
      try:
        ret = getattr(sys.modules[__name__],split[0])(split[1])          
      except:
        ret = 1

    else:

      # No test circumstance. Resolve and call the test function
      try:
        ret = getattr(sys.modules[__name__],split[0])()
      except:
        ret = 1
	
    delay=time.time()-time_before;	
    dis.set_column(4,'%s'%(my_duration(delay)))
    
    if ret == 0:
      dis.set_column(3,'OK')
      success=success+1
    else:
      dis.set_column(3,'FAILED')
      failed=failed+1
      
    if failed != 0 and stopOnFailure == True:
        break
         
    

  dis.separator()   
  dis.new_line()  
  dis.set_column(1,'%s'%(success+failed))
  dis.set_column(2,mnt)
  if failed == 0:
    dis.set_column(3,'OK')
  else:
    dis.set_column(3,'%d FAILED'%(failed))
    
  delay=time.time()-time_start    
  dis.set_column(4,'%s'%(my_duration(delay)))
  
  print ""
  dis.display()        
  
     
#___________________________________________________
def do_list():
# Display the list of all tests
#___________________________________________________

  num=int(0)
  dis = display_array(3)
  dis.new_line()  
  dis.set_column(1,'Number')
  dis.set_column(2,'Test name')
  dis.set_column(3,'Test group')
  
  dis.separator()  
  for tst in TST_BASIC:
    num=num+1
    dis.new_line()  
    dis.set_column(1,"%s"%num)
    dis.set_column(2,tst)
    dis.set_column(3,'basic')  
    
  dis.separator()         
  for tst in TST_REBUILD:
    num=num+1
    dis.new_line()  
    dis.set_column(1,"%s"%num)
    dis.set_column(2,tst)
    dis.set_column(3,'rebuild') 
    
  dis.separator()         
  for tst in TST_RW:
    num=num+1
    dis.new_line()  
    dis.set_column(1,"%s"%num)
    dis.set_column(2,tst)
    dis.set_column(3,'rw') 

  dis.separator()         
  for tst in TST_RW:
    num=num+1
    dis.new_line()  
    dis.set_column(1,"%s"%num)
    dis.set_column(2,"%s/%s"%('storageFailed',tst))
    dis.set_column(3,'storageFailed') 

  dis.separator()         
  for tst in TST_RW:
    num=num+1
    dis.new_line()  
    dis.set_column(1,"%s"%num)
    dis.set_column(2,"%s/%s"%('storageReset',tst))
    dis.set_column(3,'storageReset') 
    
  dis.separator()         
  for tst in TST_RW:
    num=num+1
    dis.new_line()  
    dis.set_column(1,"%s"%num)
    dis.set_column(2,"%s/%s"%('storcliReset',tst))
    dis.set_column(3,'storcliReset')  

  dis.display()    

#___________________________________________________
def usage():
#___________________________________________________

  print "\n./IT.py -l"
  print "  Display the whole list of tests."
  print "\n./IT.py [options] [extra] <test name/group> [<test name/group>...]"      
  print "  Runs a test list."
  print "    options:"
  print "      [--speed]          The run 4 times faster tests."
  print "      [--fast]           The run 2 times faster tests."
  print "      [--long]           The run 2 times longer tests."
  print "      [--repeat <nb>]    The number of times the test list must be repeated."   
  print "      [--stop]           To stop the tests on the 1rst failure." 
  print "      [--fusetrace]      To enable fuse trace on test. When set, --stop is automaticaly set."
  print "    extra:"
  print "      [--process <nb>]   The number of processes that will run the test in paralell. (default %d)"%(process)
  print "      [--count <nb>]     The number of loop that each process will do. (default %s)"%(loop) 
  print "      [--fileSize <nb>]  The size in MB of the file for the test. (default %d)"%(fileSize)   
  print "      [--mount <mount>]  The mount directory. (default %s)"%(DEFAULT_MNT)   
  print "    Test group and names can be displayed thanks to ./IT.py -l"
  print "       - all              designate all the tests."
  print "       - rw               designate the read/write test list."
  print "       - storageFailed    designate the read/write test list run when a storage is failed."
  print "       - storageReset     designate the read/write test list run while a storage is reset."
  print "       - storcliReset     designate the read/write test list run while the storcli is reset."
  print "       - basic            designate the non read/write test list."
  print "       - rebuild          designate the rebuild test list."
  exit(0)



#___________________________________________________
# MAIN
#___________________________________________________                  
parser = OptionParser()
parser.add_option("-v","--verbose", action="store_true",dest="verbose", default=False, help="To set the verbose mode")
parser.add_option("-p","--process", action="store",type="string", dest="process", help="The number of processes that will run the test in paralell")
parser.add_option("-c","--count", action="store", type="string", dest="count", help="The number of loop that each process will do.")
parser.add_option("-f","--fileSize", action="store", type="string", dest="fileSize", help="The size in MB of the file for the test.")
parser.add_option("-l","--list",action="store_true",dest="list", default=False, help="To display the list of test")
parser.add_option("-k","--snipper",action="store",type="string",dest="snipper", help="To start a storio/storcli snipper.")
parser.add_option("-s","--stop", action="store_true",dest="stop", default=False, help="To stop on 1rst failure.")
parser.add_option("-t","--fusetrace", action="store_true",dest="fusetrace", default=False, help="To enable fuse trace on test.")
parser.add_option("-F","--fast", action="store_true",dest="fast", default=False, help="To run 2 times faster tests.")
parser.add_option("-S","--speed", action="store_true",dest="speed", default=False, help="To run 4 times faster tests.")
parser.add_option("-L","--long", action="store_true",dest="long", default=False, help="To run 2 times longer tests.")
parser.add_option("-r","--repeat", action="store", type="string", dest="repeat", help="Test repetition count.")
parser.add_option("-m","--mount", action="store", type="string", dest="mount", help="The mount point to test on.")

# Read/write test list
TST_RW=['read_parallel','rw2','wr_rd_total','wr_rd_partial','wr_rd_random','wr_rd_total_close','wr_rd_partial_close','wr_rd_random_close','wr_close_rd_total','wr_close_rd_partial','wr_close_rd_random','wr_close_rd_total_close','wr_close_rd_partial_close','wr_close_rd_random_close']
# Basic test list
TST_BASIC=['readdir','xattr','link','rename','chmod','truncate','lock_posix_passing','lock_posix_blocking']
# Rebuild test list
TST_REBUILD=['gruyere','rebuild_fid','rebuild_one','rebuild_all']



(options, args) = parser.parse_args()
 
if options.process != None:
  process=int(options.process)
  
if options.count != None:
  loop=int(options.count)
  
if options.fileSize != None:
  fileSize=int(options.fileSize)

if options.verbose == True:
  verbose=True

if options.list == True:
  do_list()
  exit(0)
    
if options.stop == True:  
  stopOnFailure=True 

if options.fusetrace == True:  
  stopOnFailure=True 
  fuseTrace=True
  
if options.speed == True:  
  loop=loop/4
  nbGruyere=nbGruyere/4
     
elif options.fast == True:  
  loop=loop/2
  nbGruyere=nbGruyere/2
   
elif options.long == True:  
  loop=loop*2 
  nbGruyere=nbGruyere*2

if options.mount != None:
  mnt=options.mount  

site=get_site_number()

if options.snipper != None:
  NB_SID,STC_SID=get_sid_nb()
  snipper(options.snipper)
  exit(0)  
  

# Build list of test 
list=[] 
for arg in args:  
  if arg == "all":
    list.extend(TST_BASIC)
    list.extend(TST_REBUILD)
    list.extend(TST_RW)
    append_circumstance_test_list(list,TST_RW,'storageFailed')
    append_circumstance_test_list(list,TST_RW,'storageReset')
    append_circumstance_test_list(list,TST_RW,'storcliReset')   
  elif arg == "rw":
    list.extend(TST_RW)
  elif arg == "storageFailed":
    append_circumstance_test_list(list,TST_RW,arg)
  elif arg == "storageReset":
    append_circumstance_test_list(list,TST_RW,arg)
  elif arg == "storcliReset":
    append_circumstance_test_list(list,TST_RW,arg)
  elif arg == "basic":
    list.extend(TST_BASIC)
  elif arg == "rebuild":
    list.extend(TST_REBUILD)              
  else:
    list.append(arg)              
# No list of test. Print usage
if len(list) == 0:
  usage()
  
new_list=[]    
if options.repeat != None:
  repeat = int(options.repeat)
  while repeat != int(0):
    new_list.extend(list)
    repeat=repeat-1
else:
  new_list.extend(list)  

if not os.path.isdir(mnt):
  print "%s is not a directory"%(mnt)
  exit(-1)   
  
# Run the requested test list
NB_SID,STC_SID=get_sid_nb()
do_run_list(new_list)
