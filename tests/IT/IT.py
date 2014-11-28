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
EXPORT_SID_NB=int(8)
STORCLI_SID_NB=int(8)
nbGruyere=int(256)
stopOnFailure=True
fuseTrace=False
DEFAULT_MNT="mnt1_1_g0"
ALL_MNT="mnt1_1_g0,mnt2_1_g0,mnt3_1_g0,mnt4_1_g0"
mnts=DEFAULT_MNT
mnt=""
DEFAULT_RETRIES=int(20)
tst_file="tst_file"
device_number=""
mapper_modulo=""
mapper_redundancy=""
vid=""
list_cid=[]
list_sid=[]
list_host=[]  
hunic=[]
inverse=2
forward=3
safe=4
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
#___________________________________________________
def get_all_mount_points():
#___________________________________________________
  global ALL_MNT
  
  string="df"
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  ALL_MNT=""
  for line in cmd.stdout:
  
    if line.split()[0] != "rozofs":
      continue
    mount=line.split()[5]
    mount=mount.split('/')
    mount=mount[len(mount)-1] 
    if ALL_MNT == "":
      ALL_MNT=mount
    else:
      ALL_MNT=ALL_MNT+','+mount

#___________________________________________________
def get_device_numbers(hid,cid):
# Use debug interface to get the number of sid from exportd
#___________________________________________________
  device_number=1
  mapper_modulo=1
  mapper_redundancy=1 

  storio_name="storio:0"
  
  string="./build/src/rozodiag/rozodiag -i localhost%d -T storaged -c storio"%(hid)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  for line in cmd.stdout:
    if "mode" in line:
      if "multiple" in line:
        storio_name="storio:%d"%(cid)
      break; 
        
  string="./build/src/rozodiag/rozodiag -i localhost%d -T %s -c device"%(hid,storio_name)
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
  global list_cid
  global list_sid
  global list_host
  global hunic

  inst=get_rozofmount_instance()

  string="./build/src/rozodiag/rozodiag -T mount:%d:1 -c storaged_status"%(inst)       
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  storcli_sid=int(0)
  for line in cmd.stdout:
    if "UP" in line or "DOWN" in line:
      storcli_sid=storcli_sid+1
      words=line.split()
      list_cid.append(int(words[0]))
      list_sid.append(int(words[2]))
      list_host.append(int(words[4].split('localhost')[1]))
       
  hunic=[]
  for h in list_host:
    if h not in hunic:
      hunic.append(h) 
           
  string="./build/src/rozodiag/rozodiag -T export -c vfstat_stor"
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  export_sid=int(0)
  for line in cmd.stdout:
    if len(line.split()) == 0:
      continue
    if line.split()[0] != vid:
      continue;
    if "UP" in line or "DOWN" in line:
      export_sid=export_sid+1

  return export_sid,storcli_sid    
#___________________________________________________
def get_layout():
# Get the inverse forward and safe values
#___________________________________________________
  global inverse, forward, safe
  inst=get_rozofmount_instance()

  string="./build/src/rozodiag/rozodiag -T mount:%d -c layout"%(inst)       
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  for line in cmd.stdout:
    if "LAYOUT_" in line:
      words=line.split()
      layout=words[0]
      values=layout.split('_')
      inverse=values[1]
      forward=values[2]
      safe=values[3]
      return
#___________________________________________________
def export_count_sid_up ():
# Use debug interface to count the number of sid up 
# seen from the export. 
#___________________________________________________
  global vid
  
  string="./build/src/rozodiag/rozodiag -T export -c vfstat_stor"
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  match=int(0)
  for line in cmd.stdout:
    if len(line.split()) == 0:
      continue
    if line.split()[0] != vid:
      continue
    if "UP" in line:
      match=match+1

  return match
#___________________________________________________
def get_volume_id ():
  
  p = subprocess.Popen(["attr","-g","rozofs",mnt], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  for line in p.stdout:
    if "VID" in line:       
      return line.split()[2]
      
  return -1
#___________________________________________________
def get_rozofmount_instance ():

  p = subprocess.Popen(["ps","-ef"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  for proc in p.stdout:
    if not "rozofsmount/rozofsmount" in proc:
      continue
      
    for words in proc.split():
      if mnt in words.split("/"):        
	for opt in proc.split(" "):
	  if opt.startswith("instance="):
            instance=opt.split("instance=")[1]
	    return int(instance)
	    
  print "Instance of %s not found !!!\n"%(mnt)	    
  exit(-1)
#___________________________________________________
def get_site_number ():

  inst=get_rozofmount_instance()

  string="./build/src/rozodiag/rozodiag -T mount:%d -c start_config"%(inst)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  
  for line in cmd.stdout:
    words=line.split('=')
    if words[0].strip() == "running_site":    
      return int(words[1])
  return 0  
#___________________________________________________
def storcli_count_sid_available ():
# Use debug interface to count the number of sid 
# available seen from the storcli. 
#___________________________________________________

  inst=get_rozofmount_instance()
  string="./build/src/rozodiag/rozodiag -T mount:%d:1 -c storaged_status"%(inst)       
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
      print "Maximum retries reached. %s is %d\n"%(function,up)      
      return False
      
    sys.stdout.write(".")
    sys.stdout.flush()     
     
    up=getattr(sys.modules[__name__],function)()
    time.sleep(1)
   
  time.sleep(1)    
  return True  
#___________________________________________________
def loop_wait_until_less (success,retries,function):
# Loop until <function> returns <success> for a maximum 
# of <retries> attempt (one attempt per second)
#___________________________________________________

  up=int(0)

  while int(up) >= int(success):

    retries=retries-1
    if retries == 0:
      print "Maximum retries reached. %s is %d\n"%(function,up)      
      return False
      
    sys.stdout.write(".")
    sys.stdout.flush()     
     
    up=getattr(sys.modules[__name__],function)()
    time.sleep(1)
   
  time.sleep(1)    
  return True    
#___________________________________________________
def start_all_sid () :
# Wait for all sid up seen by storcli as well as export
#___________________________________________________

  for sid in range(STORCLI_SID_NB):
    hid=sid+(site*STORCLI_SID_NB)
    os.system("./setup.sh storage %s start"%(hid+1))    
    

      
#___________________________________________________
def wait_until_all_sid_up (retries=DEFAULT_RETRIES) :
# Wait for all sid up seen by storcli as well as export
#___________________________________________________
  if loop_wait_until(STORCLI_SID_NB,retries,'storcli_count_sid_available') == False: 
    print "storcli_count_sid_available %s failed"%(STORCLI_SID_NB)
    return False
  if loop_wait_until(EXPORT_SID_NB,retries,'export_count_sid_up') == False:
    print "export_count_sid_up %s failed"%(EXPORT_SID_NB)
    return False
  return True  
  
    
#___________________________________________________
def wait_until_one_sid_down (retries=DEFAULT_RETRIES) :
# Wait until one sid down seen by storcli 
#___________________________________________________

  if loop_wait_until_less(STORCLI_SID_NB,retries,'storcli_count_sid_available') == False:
    return False
  return True   
#___________________________________________________
def wait_until_x_sid_down (x,retries=DEFAULT_RETRIES) :
# Wait until one sid down seen by storcli 
#___________________________________________________

  if loop_wait_until_less(int(STORCLI_SID_NB)-int(x),retries,'storcli_count_sid_available') == False:
    return False
  return True   
#___________________________________________________
def storageStart (hid,count=int(1)) :

  sys.stdout.write("\r                                   ")
  sys.stdout.write("\rStorage start ")

  for idx in range(int(count)): 
    sys.stdout.write("%d "%(int(hid)+idx)) 
    sys.stdout.flush()
    os.system("./setup.sh storage %d start"%(int(hid)+idx))
        
#___________________________________________________
def storageStartAndWait (hid,count=int(1)) :

  storageStart(hid,count)
  time.sleep(1)
  if wait_until_all_sid_up() == True:
    return 0
        
  return 1 
#___________________________________________________
def storageStop (hid,count=int(1)) :

  sys.stdout.write("\r                                   ")
  sys.stdout.write("\rStorage stop ")

  for idx in range(int(count)): 
    sys.stdout.write("%d "%(int(hid)+idx)) 
    sys.stdout.flush()
    os.system("./setup.sh storage %d stop"%(int(hid)+idx))
  
#___________________________________________________
def storageStopAndWait (hid,count=int(1)) :

  storageStop(hid,count)
  time.sleep(1)
  wait_until_x_sid_down(count)   
    
#___________________________________________________
def storageFailed (test) :
# Run test names <test> implemented in function <test>()
# under the circumstance that a storage is stopped
#___________________________________________________
  global hunic
       
  # Wait all sid up before starting the test     
  if wait_until_all_sid_up() == False:
    return 1

  # Compute number of allowed failures
  nb_failures=int(forward)-int(inverse)
  
  # Loop on hosts
  for hid in hunic:    
           
    # Process hosts in a bunch of allowed failures	   
    if nb_failures!=1:
      if hid%nb_failures != 1:
        continue
      	    
    # Reset a bunch of storages	    
    storageStopAndWait(hid,nb_failures)
    reset_counters()
    
    # Run the test
    try:
      # Resolve and call <test> function
      ret = getattr(sys.modules[__name__],test)()         
    except:
      ret = 1
      
    # Restart every storages  
    storageStartAndWait(hid,nb_failures)  

    if ret != 0:
      return 1      

      
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
  string="./IT/IT.py --snipper storcli --mount %s"%(mnt)
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
  global hunic
    

  # Compute number of allowed failures
  nb_failures=int(forward)-int(inverse)
    
  while True:
    for hid in hunic:      

      # Process hosts in a bunch of allowed failures	   
      if nb_failures!=1:
	if hid%nb_failures != 1:
          continue

      # Wait all sid up before starting the test     
      if wait_until_all_sid_up() == False:
        return 1
          
      sys.stdout.write("\r                                 ")
      sys.stdout.flush()
      sys.stdout.write("\rStorio reset ")
      cmd=""
      for idx in range(nb_failures):
        sys.stdout.write("%d "%(int(hid)+idx))
	cmd+="./setup.sh storio %d reset;"%(int(hid)+idx)
	
      sys.stdout.flush()

      os.system(cmd)


#___________________________________________________
def storageReset (test):
# Run test names <test> implemented in function <test>()
# under the circumstance that storio(s) are periodicaly reset
#___________________________________________________
  global loop
  
 
  # Start process that reset the storages
  string="./IT/IT.py --snipper storio --mount %s"%(mnt)
  parsed = shlex.split(string)
  cmd = subprocess.Popen(parsed, stderr=subprocess.PIPE)

  saveloop=loop
  loop=loop*8
  
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
    print "Failed snipper %s"%(func)
    ret = 1
  return ret  

#___________________________________________________
def wr_rd_total ():
#___________________________________________________
  ret=os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -total -mount %s"%(process,loop,fileSize,tst_file,mnt))
  return ret  

#___________________________________________________
def wr_rd_partial ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -partial -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_rd_random ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -random -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_rd_total_close ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -total -file %s -closeAfter -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_rd_partial_close ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -partial -file %s -closeAfter -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_rd_random_close ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -random -file %s -closeAfter -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_close_rd_total ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -total -closeBetween -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_close_rd_partial ():
#___________________________________________________
  ret=os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -partial -closeBetween -mount %s"%(process,loop,fileSize,tst_file,mnt))
  return ret 

#___________________________________________________
def wr_close_rd_random ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -random -closeBetween -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_close_rd_total_close ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -total -closeBetween -closeAfter -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_close_rd_partial_close ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -partial -closeBetween -closeAfter -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def wr_close_rd_random_close ():
#___________________________________________________
  return os.system("./IT/rw -process %d -loop %d -fileSize %d -file %s -random -closeBetween -closeAfter -mount %s"%(process,loop,fileSize,tst_file,mnt))

#___________________________________________________
def rw2 ():
#___________________________________________________
  return os.system("./IT/rw2 -loop %s -file %s/%s"%(loop,mnt,tst_file))

#___________________________________________________
def prepare_file_to_read(filename,mega):
#___________________________________________________

  if not os.path.exists(filename):
    os.system("dd if=/dev/zero of=%s bs=1M count=%s 1> /dev/null"%(filename,mega))    

#___________________________________________________
def read_parallel ():
#___________________________________________________

  zefile='%s/%s'%(mnt,tst_file)
  prepare_file_to_read(zefile,fileSize) 
  ret=os.system("./IT/read_parallel -process %s -loop %s -file %s"%(process,loop,zefile)) 
  return ret   

#___________________________________________________
def xattr():
#___________________________________________________
  return os.system("./IT/test_xattr -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def link():
#___________________________________________________
  return os.system("./IT/test_link -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def readdir():
#___________________________________________________ 
  return os.system("./IT/test_readdir -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def rename():
#___________________________________________________
  ret=os.system("./IT/test_rename -process %d -loop %d -mount %s"%(process,loop,mnt))
  return ret 

#___________________________________________________
def chmod():
#___________________________________________________
  return os.system("./IT/test_chmod -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def truncate():
#___________________________________________________
  return os.system("./IT/test_trunc -process %d -loop %d -mount %s"%(process,loop,mnt))

#___________________________________________________
def lock_posix_passing():
#___________________________________________________ 
  zefile='%s/%s'%(mnt,tst_file)
  try:
    os.remove(zefile)
  except:
    pass  
  return os.system("./IT/test_file_lock -process %d -loop %d -file %s -nonBlocking"%(process,loop,zefile))  

#___________________________________________________
def lock_posix_blocking():
#___________________________________________________
  zefile='%s/%s'%(mnt,tst_file)
  try:
    os.remove(zefile)
  except:
    pass  

  ret=os.system("./IT/test_file_lock -process %d -loop %d -file %s"%(process,loop,zefile))
  return ret 

#___________________________________________________
def lock_bsd_passing():
#___________________________________________________  
  zefile='%s/%s'%(mnt,tst_file)
  try:
    os.remove(zefile)
  except:
    pass  
  return os.system("./IT/test_file_lock -process %d -loop %d -file %s -nonBlocking -bsd"%(process,loop,zefile))


#___________________________________________________
def quiet(val=10):
#___________________________________________________

  while True:
    time.sleep(val)


#___________________________________________________
def lock_bsd_blocking():
#___________________________________________________
  zefile='%s/%s'%(mnt,tst_file)
  try:
    os.remove(zefile)
  except:
    pass  
  return os.system("./IT/test_file_lock -process %d -loop %d -file %s -nonBlocking"%(process,loop,zefile))  

#___________________________________________________
def gruyere_one_reread():
# reread files create by test_rebuild utility to check
# their content
#___________________________________________________ 
  return os.system("./IT/test_rebuild -action check -nbfiles %d -mount %s"%(int(nbGruyere),mnt))
#___________________________________________________
def gruyere_file_reread(nb):
# reread files create by test_rebuild utility to check
# their content
#___________________________________________________ 
  ret=os.system("./IT/test_rebuild -action check -f %d -mount %s"%(int(nb),mnt))
  if ret != 0:
    print "File %s/rebuild/%d is corrupted"%(mnt,f)
  return ret

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
  return os.system("./IT/test_rebuild -action create -nbfiles %d -mount %s"%(int(nbGruyere),mnt))  
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
def rebuild_one_dev() :
# test rebuilding device per device
#___________________________________________________

  ret=1 
  for idx in range(len(list_sid)):

    hid=list_host[idx]
    cid=list_cid[idx]
    sid=list_sid[idx]
        
    device_number,mapper_modulo,mapper_redundancy = get_device_numbers(hid,cid)
    
    dev=hid%int(mapper_modulo)
    os.system("./setup.sh storage %d device-delete %d %d 1> /dev/null"%(hid,cid,dev))
    ret = os.system("./setup.sh storage %d device-rebuild %d %d -g %s 1> /dev/null"%(hid,cid,dev,site))
    if ret != 0:
      return ret
      
    if int(mapper_modulo) > 1:
      dev=(dev+2)%int(mapper_modulo)
      os.system("./setup.sh storage %d device-delete %d %d 1> /dev/null"%(hid,cid,dev))
      ret = os.system("./setup.sh storage %d device-rebuild %d %d -g %s 1> /dev/null"%(hid,cid,dev,site))
      if ret != 0:
	return ret
            
    ret = gruyere_one_reread()  
    if ret != 0:
      return ret 
      
  ret = gruyere_reread()          
  return ret

#___________________________________________________
def relocate_one_dev() :
# test rebuilding device per device
#___________________________________________________

  ret=1 
  for idx in range(len(list_sid)):

    hid=list_host[idx]
    cid=list_cid[idx]
    sid=list_sid[idx]
        
    ret = os.system("./setup.sh storage %d device-relocate %d 0 -g %s 1> /dev/null"%(hid,cid,site))
    if ret != 0:
      return ret
                  
    ret = gruyere_one_reread()  
    if ret != 0:
      return ret 
      
  ret = gruyere_reread()          
  return ret

#___________________________________________________
def rebuild_all_dev() :
# test re-building all devices of a sid
#___________________________________________________

  ret=1 
  for idx in range(len(list_sid)):

    hid=list_host[idx]
    cid=list_cid[idx]
    sid=list_sid[idx]

    os.system("./setup.sh storage %d device-delete %d all  1> /dev/null"%(hid,cid))
    ret = os.system("./setup.sh storage %d device-rebuild %d all -g %s 1> /dev/null"%(hid,cid,site))
    if ret != 0:
      return ret

    ret = gruyere_one_reread()  
    if ret != 0:
      return ret    

  ret = gruyere_reread()         
  return ret

#___________________________________________________
def rebuild_one_node() :
# test re-building a whole storage
#___________________________________________________
  global hunic
    
 
  ret=1 
  for hid in hunic:

    os.system("./setup.sh storage %d delete  1> /dev/null"%(hid))
    ret = os.system("./setup.sh storage %d rebuild -g %s 1> /dev/null"%(hid,site))
    if ret != 0:
      return ret

    ret = gruyere_one_reread()  
    if ret != 0:
      return ret    

  ret = gruyere_reread()         
  return ret  
#___________________________________________________
def delete_rebuild() :
# test re-building a whole storage
#___________________________________________________
  os.system("rm -rf %s/rebuild  1> /dev/null"%(mnt))
  return 0
#___________________________________________________
def rebuild_fid() :
# test rebuilding per FID
#___________________________________________________
  skip=0
  global list_cid
  global list_sid
  global list_host
  for f in range(int(nbGruyere)/10):
  
    skip=skip+1
    if skip == 4:
      skip=0  

    # Get the split of file on storages      
#    string="./setup.sh cou %s/rebuild/%d"%(mnt,f+1)
    string="attr -g rozofs %s/rebuild/%d"%(mnt,f+1)
    parsed = shlex.split(string)
    cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # loop on the bins file constituting this file, and ask
    # the storages for a rebuild of the file
    bins_list = []
    fid=""
    cid=0
    storages=""
    for line in cmd.stdout:
	  
      if "FID" in line:
        words=line.split();
	if len(words) >= 2:
          fid=words[2]
	  continue
	  
      if "CLUSTER" in line:
        words=line.split();
	if len(words) >= 2:
          cid=words[2]
	  continue
	  
      if "STORAGE" in line:
        words=line.split();
	if len(words) >= 2:
          storages=words[2]
	  continue	  	  	    
      
    # loop on the bins file constituting this file, and ask
    # the storages for a rebuild of the file
    line_nb=0
    for sid in storages.split('-'):
    
      sid=int(sid)
      
      line_nb=line_nb+1
      if skip >= line_nb:
	  continue;  

      hid=-1
      for idx in range(len(list_sid)):
        if int(list_cid[idx]) == int(cid) and int(list_sid[idx]) == int(sid):
          hid=list_host[idx]
          break

      if hid == -1:
        print "No such cid/sid %d/%d\n"%(cid,sid)
	continue;
	
      string="./setup.sh storage %s fid-rebuild -g %s -s %s/%s -f %s 1> /dev/null"%(hid,site,cid,sid,fid)
      parsed = shlex.split(string)
      cmd = subprocess.Popen(parsed, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
      cmd.wait()

      if cmd.returncode != 0:
        print "%s failed"%(string)
	return 1 	       
  return gruyere_reread()  

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
def do_compile_programs(): 
# compile all program if program.c is younger
#___________________________________________________
  dirs=os.listdir("%s/IT"%(os.getcwd()))
  for file in dirs:
    if ".c" not in file:
      continue
    words=file.split('.')
    prg=words[0]   
    do_compile_program("IT/%s"%(prg))

#___________________________________________________
def do_run_list(list):
# run a list of test
#___________________________________________________
  global tst_file
  
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
    
    sys.stdout.write( "\r___%4d/%d : %-40s \n"%(tst_num,total_tst,tst))

    dis.new_line()  
    dis.set_column(1,'%s'%(tst_num))
    dis.set_column(2,tst)

    
    # Split optional circumstance and test name
    split=tst.split('/') 
    
    time_before=time.time()
    reset_counters()   
    tst_file="tst_file" 
    
    if len(split) > 1:
    
      tst_file="%s.%s"%(split[1],split[0])
    
      # There is a test circumstance. resolve and call the circumstance  
      # function giving it the test name
      try:
        ret = getattr(sys.modules[__name__],split[0])(split[1])          
      except:
        ret = 1

    else:

      tst_file=split[0]

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

  print "\n./IT/IT.py -l"
  print "  Display the whole list of tests."
  print "\n./IT/IT.py [options] [extra] <test name/group> [<test name/group>...]"      
  print "  Runs a test list."
  print "    options:"
  print "      [--speed]          The run 4 times faster tests."
  print "      [--fast]           The run 2 times faster tests."
  print "      [--long]           The run 2 times longer tests."
  print "      [--repeat <nb>]    The number of times the test list must be repeated."   
  print "      [--cont]           To continue tests on failure." 
  print "      [--fusetrace]      To enable fuse trace on test. When set, --stop is automaticaly set."
  print "    extra:"
  print "      [--process <nb>]   The number of processes that will run the test in paralell. (default %d)"%(process)
  print "      [--count <nb>]     The number of loop that each process will do. (default %s)"%(loop) 
  print "      [--fileSize <nb>]  The size in MB of the file for the test. (default %d)"%(fileSize)   
  print "      [--mount <mount1,mount2,..>]  A comma separated list of mount points. (default %s)"%(mnts) 
  print "    Test group and names can be displayed thanks to ./IT/IT.py -l"
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
parser.add_option("-s","--cont", action="store_true",dest="cont", default=False, help="To continue on failure.")
parser.add_option("-t","--fusetrace", action="store_true",dest="fusetrace", default=False, help="To enable fuse trace on test.")
parser.add_option("-F","--fast", action="store_true",dest="fast", default=False, help="To run 2 times faster tests.")
parser.add_option("-S","--speed", action="store_true",dest="speed", default=False, help="To run 4 times faster tests.")
parser.add_option("-L","--long", action="store_true",dest="long", default=False, help="To run 2 times longer tests.")
parser.add_option("-r","--repeat", action="store", type="string", dest="repeat", help="A repetition count.")
parser.add_option("-m","--mount", action="store", type="string", dest="mount", help="A comma separated list of mount points to test on.")

# Read/write test list
TST_RW=['read_parallel','rw2','wr_rd_total','wr_rd_partial','wr_rd_random','wr_rd_total_close','wr_rd_partial_close','wr_rd_random_close','wr_close_rd_total','wr_close_rd_partial','wr_close_rd_random','wr_close_rd_total_close','wr_close_rd_partial_close','wr_close_rd_random_close']
# Basic test list
TST_BASIC=['readdir','xattr','link','rename','chmod','truncate','lock_posix_passing','lock_posix_blocking']
# Rebuild test list
TST_REBUILD=['gruyere','rebuild_fid','rebuild_one_dev','relocate_one_dev','rebuild_all_dev','rebuild_one_node']

list_cid=[]
list_sid=[]
list_host=[]

get_all_mount_points()

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
    
if options.cont == True:  
  stopOnFailure=False 

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
  mnts=options.mount
  if mnts == "all":
    mnts=ALL_MNT
else:
  mnts=ALL_MNT.split(',')[0]
mnt=mnts.split(',')[0]  
get_layout()

if options.snipper != None:
  site=get_site_number()
  EXPORT_SID_NB,STORCLI_SID_NB=get_sid_nb() 
  snipper(options.snipper)
  exit(0)  
  
#TST_REBUILD=TST_REBUILD+['rebuild_delete']

# Build list of test 
list=[] 
for arg in args:  
  if arg == "all":
    list.extend(TST_BASIC)
    list.extend(TST_REBUILD)
    list.extend(TST_RW)
    append_circumstance_test_list(list,TST_RW,'storageFailed')
    append_circumstance_test_list(list,TST_RW,'storageReset')
#re    append_circumstance_test_list(list,TST_RW,'storcliReset')   
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

do_compile_programs() 

for mnt in mnts.split(','):

  mnt=mnt.split('/')[0]
  site=get_site_number()
  get_layout()
  vid=get_volume_id()
  EXPORT_SID_NB,STORCLI_SID_NB=get_sid_nb()

  if not os.path.isdir(mnt):
    print "%s is not a directory"%(mnt)
    continue 

  do_run_list(new_list)
