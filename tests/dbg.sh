exp_hosts="localhost"
exp_ports="50000"

fs_hosts="localhost"
fs_ports="50003"

stc_hosts="localhost"
stc_ports="50004 50005"

st_hosts=" localhost1 localhost2 localhost3 localhost4"
std_ports="50027"
io_ports="50028 50029 50030 50031"

 
syntax() {
  echo "$name [verbose] [exp|fs|stc|st|std|io|all] [cmd1 [cmd2 [...]]]"
  exit
}
do_ask () {
 for h in $H
 do
   for p in $P
   do
     echo
     echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"     
     printf  "_______ %9s _______________ %s:%s\n" $WHAT $h $p   
     /root/rozofs/tests/build/src/debug/rozodebug -t 1 -i $h -p $p $cmd
   done
 done
} 
ask_exp () {
  WHAT="EXPORT"
  H=$exp_hosts
  P=$exp_ports
  do_ask
}
ask_fs () {
  WHAT="FS MOUNT"
  H=$fs_hosts
  P=$fs_ports
  do_ask
}
ask_stc () {
  WHAT="STORCLI"
  H=$stc_hosts
  P=$stc_ports
  do_ask
}
ask_std () {
  WHAT="STORAGED"
  H=$st_hosts
  P=$std_ports
  do_ask
}
ask_io () {
  WHAT="STORIO"
  H=$st_hosts
  P=$io_ports
  do_ask
}
ask_st () {
  ask_std
  ask_io
}
ask_all () {
  ask_exp
  ask_fs
  ask_stc
  ask_st
}

name=`basename $0`

case "$1" in
  -*|?) syntax;;
esac  

if [ $1 == "verbose" ];
then 
  set -x
  shift 1
fi

case "$1" in
  exp|fs|stc|st|std|io|all) who=$1; shift 1;;
  *)                        who=all;;
esac    
  
cmd="-c who"  
while [ ! -z $1 ];
do
  cmd=`echo "$cmd -c $1"`
  shift 1
done  

case "$who" in
  exp) ask_exp;; 
  fs)  ask_fs;; 
  stc) ask_stc;; 
  st)  ask_st;; 
  std) ask_std;; 
  io)  ask_io;; 
  all) ask_all;;
esac
