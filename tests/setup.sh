#!/bin/bash

#  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
#  This file is part of Rozofs.
#  Rozofs is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published
#  by the Free Software Foundation, version 2.
#  Rozofs is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.

#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see
#  <http://www.gnu.org/licenses/>.

#
# setup.sh will generates a full working rozofs locally
# it's a useful tool for testing and debugging purposes. 
#
. env.sh 2> /dev/null

COREDIR="/var/run/rozofs_core"
# $1 is the site number
create_site() {
ROZOFS_SITE_PATH=/usr/local/etc/rozofs/

   mkdir -p $ROZOFS_SITE_PATH
   echo $1 > $ROZOFS_SITE_PATH/rozofs_site
}
# Input host number
# output gid, hid, cid, sid
resolve_host_storage() {
    
  # Geo replication case
  hid=$1
  if [ $hid -gt $STORAGES_PER_SITE ]
  then
    gid=1
    sid=$(( hid - STORAGES_PER_SITE ))
  else
    gid=0
    sid=$hid
  fi  
  cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))
  STORAGE_CONF=${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${cid}'_'${hid}'.conf'  
}

process_killer () { 

  if ls /var/run/$1* > /dev/null 2>&1
  then
    for pid in `cat /var/run/$1* `
    do
      case "$pid" in
        0);;
        *) kill $pid  > /dev/null 2>&1 ;;
      esac	    
    done
  else
    return  
  fi

  #sleep 2
      
  if ls /var/run/$1* > /dev/null 2>&1
  then   
    for pid in `cat /var/run/$1* `
    do
      case "$pid" in
        0);;
	*) kill -9 $pid > /dev/null 2>&1 ;; 
      esac	
    done
  fi  
}   
build ()
{
    if [ ! -e "${LOCAL_SOURCE_DIR}" ]
    then
        echo "Unable to build RozoFS (${LOCAL_SOURCE_DIR} not exist)"
    fi

    if [ -e "${LOCAL_BUILD_DIR}" ]
    then
        rm -rf ${LOCAL_BUILD_DIR}
    fi

    mkdir ${LOCAL_BUILD_DIR}

    cd ${LOCAL_BUILD_DIR}
    rm -f ${LOCAL_SOURCE_DIR}/CMakeCache.txt
    cmake -G "Unix Makefiles" -DROZOFS_BIN_DIR=${ROZOFS_BIN_DIR} -DROZOFS_SHELL_DIR=${ROZOFS_SHELL_DIR}  -DDAEMON_PID_DIRECTORY=${BUILD_DIR} -DCMAKE_BUILD_TYPE=${LOCAL_CMAKE_BUILD_TYPE} ${LOCAL_SOURCE_DIR}
    make
    cd ..
    cp -r ${LOCAL_SOURCE_DIR}/tests/fs_ops/pjd-fstest/tests ${LOCAL_PJDTESTS}
}


rebuild ()
{
    if [ ! -e "${LOCAL_SOURCE_DIR}" ]
    then
        echo "Unable to build RozoFS (${LOCAL_SOURCE_DIR} not exist)"
    fi

    cd ${LOCAL_BUILD_DIR}
    make
    cd ..
    cp -r ${LOCAL_SOURCE_DIR}/tests/fs_ops/pjd-fstest/tests ${LOCAL_PJDTESTS}
}

gen_storage_conf ()
{
    STORAGES_BY_CLUSTER=$1
    PORT_PER_STORAGE_HOST=$2

    if [ ! -e "$LOCAL_CONF" ]
    then
	mkdir -p $LOCAL_CONF
    fi

    for hid in $(seq ${STORAGES_TOTAL}); do

        # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
        resolve_host_storage $hid

	if [ -e "$STORAGE_CONF" ]
	then
            rm -rf $STORAGE_CONF
	fi

	touch $STORAGE_CONF

	echo "#${NAME_LABEL}" >> $STORAGE_CONF
	echo "#${DATE_LABEL}" >> $STORAGE_CONF

	printf "threads = $NB_DISK_THREADS;\n" >> $STORAGE_CONF
	printf "nbcores = $NB_CORES;\n" >> $STORAGE_CONF
	printf "storio  = \"$STORIO_MODE\";" >> $STORAGE_CONF

	printf "listen = ( \n" >> $STORAGE_CONF
	printf "  {addr = \"192.168.2.$hid\"; port = 41000;}" >> $STORAGE_CONF

	# Test for special character "*"
	#printf "  {addr = \"*\"; port = 4100$sid;}" >> $STORAGE_CONF

	for idx in $(seq 2 1 ${PORT_PER_STORAGE_HOST}); do
            printf " ,\n  {addr = \"192.168.$((idx+1)).$hid\"; port = 41000;}"
	done >>  $STORAGE_CONF

	printf "\n);\n" >>  $STORAGE_CONF
	echo 'storages = (' >> $STORAGE_CONF
	echo "  {cid = $cid; sid = $sid; root =\"${LOCAL_STORAGES_ROOT}_$cid-$hid\"; device-total = $NB_DEVICE_PER_SID; device-mapper = $NB_DEVICE_MAPPER_PER_SID; device-redundancy = $NB_DEVICE_MAPPER_RED_PER_SID;}" >> $STORAGE_CONF
	echo ');' >> $STORAGE_CONF
    done;	
}
start_one_storage() 
{
    case $1 in
	"all") start_storaged ${STORAGES_BY_CLUSTER} 0; return;;
    esac


    hid=$1	       
    # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
    resolve_host_storage $hid

    #echo "Start storage cid: $cid sid: $sid"
    rozolauncher start /var/run/launcher_storaged_${LOCAL_STORAGE_NAME_BASE}$hid.pid  ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_DAEMON} -c $STORAGE_CONF -H ${LOCAL_STORAGE_NAME_BASE}$hid &
    #sleep 1
}
reset_one_storio() 
{
    case "$1" in
      "") usage;;
    esac


    hid=$1	       
    # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
    resolve_host_storage $hid

    #echo "Start storage cid: $cid sid: $sid"
    pid=`ps -ef | grep "storio -i" | grep -v rozolauncher | grep " -c $STORAGE_CONF" | awk '{ print $2 }'`
    kill -9 $pid
    #sleep 1
}
gen_export_conf ()
{

    ROZOFS_LAYOUT=$1

    FILE=${LOCAL_CONF}'export_l'${ROZOFS_LAYOUT}'.conf'

    if [ ! -e "$LOCAL_CONF" ]
    then
        mkdir -p $LOCAL_CONF
    fi

    if [ -e "$FILE" ]
    then
        rm -rf $FILE
    fi

    sid=0

    touch $FILE
    echo "#${NAME_LABEL}" >> $FILE
    echo "#${DATE_LABEL}" >> $FILE
#    echo "layout = ${ROZOFS_LAYOUT} ;" >> $FILE
    echo "layout = 2 ;" >> $FILE
    echo 'volumes =' >> $FILE
    echo '      (' >> $FILE

    for v in $(seq ${NB_VOLUMES}); do

        echo '        {' >> $FILE
        echo "            vid = $v;" >> $FILE
	echo "            layout = $ROZOFS_LAYOUT;" >> $FILE
	
	if [ "$GEOREP" -ne 1 ];
	then
	  echo "            georep = True;" >> $FILE	  
	fi
        echo '            cids= ' >> $FILE
        echo '            (' >> $FILE

        for c in $(seq ${NB_CLUSTERS_BY_VOLUME}); do

            let idx_cluster=(${v}-1)*${NB_CLUSTERS_BY_VOLUME}+${c}

            echo '                   {' >> $FILE
            echo "                       cid = $idx_cluster;" >> $FILE
            echo '                       sids =' >> $FILE
            echo '                       (' >> $FILE

            for k in $(seq ${STORAGES_BY_CLUSTER}); do
	    
                sid=$((sid+1))                    

	       	if [ "$GEOREP" -ne 1 ];
		then
		    sid_geo=$((sid+STORAGES_PER_SITE))
                    if [[ ${k} == ${STORAGES_BY_CLUSTER} ]]
                    then
                	echo "                           {sid = ${sid}; site0 = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";site1 = \"${LOCAL_STORAGE_NAME_BASE}${sid_geo}\";}" >> $FILE
                    else
                	echo "                           {sid = ${sid}; site0 = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";site1 = \"${LOCAL_STORAGE_NAME_BASE}${sid_geo}\";}," >> $FILE
                    fi
		else
                    if [[ ${k} == ${STORAGES_BY_CLUSTER} ]]
                    then
                        echo "                           {sid = ${sid}; host = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";}" >> $FILE
                    else
                        echo "                           {sid = ${sid}; host = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";}," >> $FILE
                    fi		  
		fi    
            done;

            echo '                       );' >> $FILE
            if [[ ${c} == ${NB_CLUSTERS_BY_VOLUME} ]]
            then
                echo '                   }' >> $FILE
            else
                echo '                   },' >> $FILE
            fi
        done;
    echo '            );' >> $FILE
    if [[ ${v} == ${NB_VOLUMES} ]]
    then
        echo '        }' >> $FILE
    else
        echo '        },' >> $FILE
    fi
    done;
    echo '    )' >> $FILE
    echo ';' >> $FILE
    
    echo 'exports = (' >> $FILE
    for k in $(seq ${NB_EXPORTS}); do
        if [[ ${k} == ${NB_EXPORTS} ]]
        then
            echo "   {eid = $k; bsize = ${EXPORT_BSIZE[k-1]}; root = \"${LOCAL_EXPORTS_ROOT}_$k\"; md5=\"${4}\"; squota=\"$SQUOTA\"; hquota=\"$HQUOTA\"; vid=${EXPORT_VID[k-1]};}" >> $FILE
        else
            echo "   {eid = $k; bsize = ${EXPORT_BSIZE[k-1]};root = \"${LOCAL_EXPORTS_ROOT}_$k\"; md5=\"${4}\"; squota=\"$SQUOTA\"; hquota=\"$HQUOTA\"; vid=${EXPORT_VID[k-1]};}," >> $FILE
        fi
    done;
    echo ');' >> $FILE
}
geomgr_modify ()
{
    FILE=${LOCAL_CONF}'geomgr.conf'


    # No personalized geomgr configuration file up to now
    if [ ! -f ${WORKING_DIR}/geomgr.conf ];
    then
    
      if [ ! -f $FILE ];
      then
        echo "$FILE does not exist  ! You should re-start rozo"
	exit
      fi	
      cp $FILE ${WORKING_DIR}/geomgr.conf
    fi  
    
    nedit ${WORKING_DIR}/geomgr.conf
    cp ${WORKING_DIR}/geomgr.conf $FILE
}
geomgr_delete ()
{   
    FILE=${LOCAL_CONF}'geomgr.conf'


    # No personalized geomgr configuration file up to now
    if [ -f ${WORKING_DIR}/geomgr.conf ];
    then
      rm ${WORKING_DIR}/geomgr.conf
    fi  
    
    gen_geomgr_conf
}
gen_geomgr_conf ()
{

    FILE=${LOCAL_CONF}'geomgr.conf'

    if [ ! -e "$LOCAL_CONF" ]
    then
        mkdir -p $LOCAL_CONF
    fi

    if [ -e "$FILE" ]
    then
        rm -rf $FILE
    fi
    
    # When there is a saved geomgr configuration
    # use it
    if [ -f ${WORKING_DIR}/geomgr.conf ];
    then
      echo "Use for geo-replication ${WORKING_DIR}/geomgr.conf"
      cp ${WORKING_DIR}/geomgr.conf $FILE
      return
    fi  

    echo "Generate $FILE"
    
    touch $FILE
    echo "#${NAME_LABEL}" >> $FILE
    echo "#${DATE_LABEL}" >> $FILE
    if [ "$GEOREP" -ne 1 ];
    then
      echo "active = True ;" >> $FILE
    else
      echo "active = False ;" >> $FILE
    fi 
    echo "export-daemons = (" >> $FILE
    echo "   {" >> $FILE
    echo "	active = True;" >> $FILE
    echo "	host   = \"${EXPORT_HOST}\";" >> $FILE
    echo "	exports="   >> $FILE
    echo "	("   >> $FILE
    for k in $(seq ${NB_EXPORTS}); do
      echo "          {" >> $FILE
      echo "               active = True;"  >> $FILE
      echo "               path   = \"${LOCAL_EXPORTS_ROOT}_$k\";" >> $FILE
      echo "               site   = 1;" >> $FILE
      echo "               nb     = 1;" >> $FILE
      echo "          }," >> $FILE
      echo "          {" >> $FILE
      echo "               active = True;"  >> $FILE
      echo "               path   = \"${LOCAL_EXPORTS_ROOT}_$k\";" >> $FILE
      echo "               site   = 0;" >> $FILE
      echo "               nb     = 1;" >> $FILE
      echo "               calendar =" >> $FILE
      echo "		   (" >> $FILE
      echo "		     { start=\"8:00\"; stop=\"12:15\";  },">> $FILE
      echo "		     { start=\"14:15\"; stop=\"17:30\"; }">> $FILE
      echo "		   );" >> $FILE
      if [[ ${k} == ${NB_EXPORTS} ]]
      then
        echo "          }" >> $FILE            
      else
        echo "          }," >> $FILE            
      fi
    done;
    echo "	);"   >> $FILE
    echo "   }" >> $FILE
    echo ');' >> $FILE
}
rebuild_storage_fid() 
{
    hid=$2
    # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
    resolve_host_storage $hid
    shift 3

    cmd="${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_REBUILD} -c $STORAGE_CONF -H ${LOCAL_STORAGE_NAME_BASE}$hid -r ${EXPORT_HOST} $*"
    echo $cmd
    $cmd
}
rebuild_storage_device() 
{
    hid=$1
    # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
    resolve_host_storage $hid

    case "$2" in
      "")   usage "Missing device identifier";;
      all)  dev="";;
      *)    dev="-d $2";;
    esac

    echo "rebuild $cid/$sid device $2"     
    create_storage_device $hid $2
    
    ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_REBUILD} -c $STORAGE_CONF -H ${LOCAL_STORAGE_NAME_BASE}$hid -r ${EXPORT_HOST} $3 $4 --sid $cid/$sid $dev 
}
delete_storage_device() 
{
    hid=$1
    # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
    resolve_host_storage $hid

    case "$2" in
      "")   usage "Missing device identifier";;
      all)  begin=0  ; end=$((NB_DEVICE_PER_SID-1));;
      *)    begin=$2 ; end=$2;;
    esac
    for device in $(seq $begin $end)
    do

      dir="${LOCAL_STORAGES_ROOT}_$cid-$hid/$device"
      if [ -d $dir ];
      then
        echo "delete ${LOCAL_STORAGE_NAME_BASE}$hid device $device : $dir" 
        \rm -rf $dir
      else
        echo "$dir does not exist !!!"         	  
      fi
    done  
}
create_storage_device() 
{
    hid=$1
    # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
    resolve_host_storage $hid
		    

    case "$2" in
      "")  begin=0  ; end=$((NB_DEVICE_PER_SID-1));;
      all) begin=0  ; end=$((NB_DEVICE_PER_SID-1));;
      *)   begin=$2 ; end=$2;;
    esac
    for device in $(seq $begin $end)
    do
      mkdir ${LOCAL_STORAGES_ROOT}_$cid-$hid/$device > /dev/null 2>&1
    done  
}
stop_one_storage () {
   case $1 in
     "all") stop_storaged; return;;
   esac
   rozolauncher stop /var/run/launcher_storaged_${LOCAL_STORAGE_NAME_BASE}$1.pid   
}   
reset_one_storage () {
  stop_one_storage $1
  #sleep 1
  start_one_storage $1
}
# $1 = STORAGES_BY_CLUSTER
start_storaged ()
{

    STORAGES_BY_CLUSTER=$1
    
    echo "------------------------------------------------------"
    echo "Start ${LOCAL_STORAGE_DAEMON}"

    for hid in $(seq ${STORAGES_TOTAL}); do

        # Resolve STORAGE_CONF as well as gid, hid, cid, sid   
        resolve_host_storage $hid
    
        cmd="rozolauncher start /var/run/launcher_storaged_${LOCAL_STORAGE_NAME_BASE}${hid}.pid ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_DAEMON} -c $STORAGE_CONF -H ${LOCAL_STORAGE_NAME_BASE}${hid}"
	echo $cmd
	$cmd &	    		  
    done	
}

stop_storaged()
{
   echo "------------------------------------------------------"
   echo "Stopping the storaged"
    
   for hid in $(seq ${STORAGES_TOTAL}); do
       stop_one_storage $hid
   done    
}
reload_storaged ()
{
    echo "------------------------------------------------------"
    echo "Reload ${LOCAL_STORAGE_DAEMON}"
    kill -1 `ps ax | grep ${LOCAL_STORAGE_DAEMON} | grep -v grep | awk '{print $1}'`
}
create_storages ()
{

    for hid in $(seq ${STORAGES_TOTAL}); do

        resolve_host_storage $hid		    
        fname=${LOCAL_STORAGES_ROOT}_${cid}-${hid}

        if [ -e "$fname" ]
        then
            rm -rf $fname/*.bins
        else
            mkdir -p $fname
        fi

	create_storage_device $hid all
    done;	
}
# $1 -> storages by node
remove_storages ()
{
    for hid in $(seq ${STORAGES_TOTAL}); do

	if [ -e "${LOCAL_STORAGES_ROOT}_${i}-${sid}" ]
	then
            rm -rf ${LOCAL_STORAGES_ROOT}_${i}-${sid}
	fi

    done;
}

# $1 -> LAYOUT
# $2 -> NB STORAGES BY CLUSTER
go_layout ()
{
    STORAGES_BY_CLUSTER=$2

    if [ ! -e "${LOCAL_CONF}export_l${ROZOFS_LAYOUT}.conf" ] || [ ! -e "${LOCAL_CONF}export_l${ROZOFS_LAYOUT}.conf" ]
    then
        echo "Unable to change configuration files to layout ${ROZOFS_LAYOUT}"
        exit 0
    fi
    
    ln -s -f ${LOCAL_CONF}'export_l'${ROZOFS_LAYOUT}'.conf' ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}
    for hid in $(seq ${STORAGES_TOTAL}); do
        # Resolve STORAGE_CONF	and gid, hid, cid, sid   
        resolve_host_storage $hid		    
        ln -s -f $STORAGE_CONF ${LOCAL_CONF}'_'${cid}'_'${hid}"_"${LOCAL_STORAGE_CONF_FILE}
    done;

}

deploy_clients_local ()
{
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to mount RozoFS (configuration file doesn't exist)"
	return
    fi	

    NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`
    fs_instance=0
    geo_instance=0

    for eid in $(seq ${NB_EXPORTS}); do
        
        for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do
	
	    for geo_site in $(seq ${GEOREP}); do
	    
        	mount_point=${LOCAL_MNT_ROOT}${eid}_${idx_client}_g$((geo_site-1))
        	mountpoint -q ${mount_point}


        	if [ "$?" -ne 0 ]
        	then

                    echo "__Mount RozoFS ${LOCAL_EXPORTS_NAME_PREFIX}_${eid} on ${mount_point}"

                    if [ ! -e "${mount_point}" ]
                    then
                	mkdir -p ${mount_point}
                    fi

                    option=" -o rozofsexporttimeout=24 -o rozofsstoragetimeout=4 -o rozofsstorclitimeout=11"
                    option="$option -o nbcores=$NB_CORES"
                    option="$option -o rozofsbufsize=$WRITE_FILE_BUFFERING_SIZE -o rozofsminreadsize=$READ_FILE_MINIMUM_SIZE" 
                    option="$option -o rozofsnbstorcli=$NB_STORCLI"
                    option="$option -o rozofsshaper=$SHAPER"
                    option="$option -o posixlock"
                    option="$option -o bsdlock"
                    option="$option -o rozofsrotate=3"		
		    option="$option -o site=$((geo_site-1))"	    
                    option="$option -o instance=$fs_instance"
                    fs_instance=$((fs_instance+1))
		    
                    cmd="${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${EXPORT_HOST} -E ${LOCAL_EXPORTS_ROOT}_${eid} ${mount_point} ${option}"
                    echo $cmd
		    $cmd
        	else
                    echo "Unable to mount RozoFS (${mount_point} already mounted)"
        	fi
            done;
        done;
    done;
    
    # Start geocli manager
    cmd="rozolauncher start /var/run/launcher_geomgr.pid geomgr -c ${LOCAL_CONF}geomgr.conf -t 5"
    echo "__$cmd"
    $cmd &      

}
rozofsmount_kill_best_effort()
{
    #echo "------------------------------------------------------"
    echo "Killing rozofsmount and storcli in best effort mode"
    process_killer rozofsmount
}
geocli_kill_best_effort()
{
    echo "------------------------------------------------------"
    echo "Killing geomgr"
    rozolauncher stop /var/run/launcher_geomgr.pid
}
undeploy_clients_local ()
{
    geocli_kill_best_effort
    
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
        then
        echo "Unable to umount RozoFS (configuration file doesn't exist)"
	return
    fi	

    NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

    for eid in $(seq ${NB_EXPORTS}); do
        for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do

            mount_point0=${LOCAL_MNT_ROOT}${eid}_${idx_client}_g0
            mount_point1=${LOCAL_MNT_ROOT}${eid}_${idx_client}_g1

            echo "__Umount RozoFS ${LOCAL_MNT_PREFIX}${eid} instance ${idx_client}"

            umount $mount_point0
	    case $? in
	      0) ;;
	      *) umount -l $mount_point0;;
	    esac  
            rm -rf $mount_point0

            umount $mount_point1
	    case $? in
	      0) ;;
	      *) umount -l $mount_point1;;
	    esac  
            rm -rf $mount_point1		

        done

    done

    sleep 0.4
    rozofsmount_kill_best_effort
    
}

start_exportd ()
{
    echo "------------------------------------------------------"
    echo "Start ${LOCAL_EXPORT_DAEMON}"
    ${LOCAL_BINARY_DIR}/exportd/${LOCAL_EXPORT_DAEMON} -c ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}
    
#    sleep 1
#    cmd=`ps -ef | grep exportd | grep export.conf | awk '{print $8" -pid "$2 }'`
#    ddd $cmd &
}

stop_exportd ()
{
    echo "------------------------------------------------------"
    echo "Killing exportd"
    process_killer exportd.pid 
}

reset_exportd () {
    stop_exportd
    start_exportd 
}

reload_exportd ()
{
    echo "------------------------------------------------------"
    PID=`ps ax | grep ${LOCAL_EXPORT_DAEMON} | grep -v grep | awk '{print $1}'`
    if [ "$PID" != "" ]
    then
        echo "Reload ${LOCAL_EXPORT_DAEMON} (PID: ${PID})"
        kill -1 $PID
    else
        echo "Unable to reload ${LOCAL_EXPORT_DAEMON} (not running)"
    fi
}

# $1 -> Nb. of exports
create_exports ()
{
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to create export directories (configuration file doesn't exist)"
	return
    fi
    
    NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

    for eid in $(seq ${NB_EXPORTS}); do
        if [ -e "${LOCAL_EXPORTS_ROOT}_${eid}" ]
        then
            rm -rf ${LOCAL_EXPORTS_ROOT}_${eid}/*
        else
            mkdir -p ${LOCAL_EXPORTS_ROOT}_${eid}
        fi
    done;
}

# $1 -> Nb. of exports
remove_exports ()
{
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to remove export directories (configuration file doesn't exist)"
	return
    fi
    
    NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

    for eid in $(seq ${NB_EXPORTS}); do

        if [ -e "${LOCAL_EXPORTS_ROOT}_${eid}" ]
        then
            rm -rf ${LOCAL_EXPORTS_ROOT}_${eid}
        fi
    done;
}

remove_config_files ()
{
    echo "------------------------------------------------------"
    echo "Remove configuration files"
    rm -rf $LOCAL_CONF
}

remove_all ()
{
    echo "------------------------------------------------------"
    echo "Remove configuration files, storage and exports directories"
    rm -rf $LOCAL_CONF
    rm -rf $LOCAL_STORAGES_ROOT*
    rm -rf $LOCAL_EXPORTS_ROOT*
}

remove_build ()
{
    echo "------------------------------------------------------"
    echo "Remove build directory"
    rm -rf $LOCAL_BUILD_DIR
}

do_start_all_processes() {
     start_storaged ${STORAGES_BY_CLUSTER}
     #start_expgw
     start_exportd 1
     deploy_clients_local
}


do_pause() {
    undeploy_clients_local
    stop_storaged
    stop_exportd
    #stop_expgw
}

do_stop()
{
    do_pause
    remove_all
    #sleep 1
}

clean_all ()
{
    do_stop
    remove_build
}

get_bin_complete_name () {
  case "$1" in
  storaged|storio) bin=${LOCAL_BINARY_DIR}/$storaged_dir/$1;;
  exportd_slave)   bin=${LOCAL_BINARY_DIR}/exportd/exportd;;
  geomgr)     bin=${LOCAL_BINARY_DIR}/geocli/geomgr;;
  *)               bin=${LOCAL_BINARY_DIR}/$1/$1;;
  esac
}
do_listCore() {
  if [ -d $COREDIR ];
  then
    
    cd $COREDIR
    for dir in `ls `
    do
    
      get_bin_complete_name $dir

      for file in `ls $dir`
      do
        res=`ls -lh $dir/$file | awk '{print $5" "$6" "$7" "$8" "$9}'`
        if [ $dir/$file -nt $bin ];
	then
	  echo "(NEW) $res"
	else
	  echo "(OLD) $res"
	fi
      done
    done
    
  fi    
} 
do_removeCore() {
  shift 1
  if [ ! -d $COREDIR ];
  then
    return
  fi  
  cd $COREDIR
  
  case "$1" in
    all) {
      for dir in `ls `
      do
        for file in `ls $dir`
        do
          unlink $dir/$file
        done
      done
      return
    };;
  esac
    
  while [ ! -z "$1" ];
  do
    unlink $1
    shift 1
  done  
}   
do_debugCore () {
  name=`echo $1 | awk -F'/' '{ print $1}'`
  
  get_bin_complete_name $name
  ddd $bin -core $COREDIR/$1 &
}
do_core () 
{
  shift 1
  
  case "$1" in
  "")       do_listCore;;
  "remove") do_removeCore $*;;
  *)        do_debugCore $1;;
  esac      
}
do_cou() {
  shift 1
  while [ ! -z $1 ];
  do
    do_one_cou $1
    shift 1
  done
}
do_one_cou () 
{
  COUFILE=/tmp/.cou

  
  case "$1" in
  "")       syntax;;
  esac      


  if [ ! -e $1 ];
  then
    printf "%20s  does not exist !!!\n" $1
    return 
  fi

  attr -g rozofs $1 > $COUFILE
  if [ ! -s $COUFILE ];
  then
    printf "%20s is not a RozoFS object !!!\n" $1
    return 
  fi
  
  printf "\n ___________ $1 ___________\n"
  cat $COUFILE
  
  mode=`awk '{if ($1=="MODE") printf $3; }' $COUFILE`
 #  BSIZE=`awk '{if ($1=="BSIZE") printf $3; }' $COUFILE`
  
  fid=`awk '{if ($1=="FID") printf $3; }' $COUFILE`
  slice=`awk '{if ($1=="SLICE") printf $3; }' $COUFILE`
#  lay=`awk '{if ($1=="LAYOUT") printf $3; }' $COUFILE`
  dist=`awk '{if ($1=="STORAGE") printf $3; }' $COUFILE`
  cluster=`awk '{if ($1=="CLUSTER") printf $3; }' $COUFILE`
  SID_LIST=`echo $dist | awk -F'-' '{ for (i=1;i<=NF;i++) print " "$i; }'`
  eid=`awk '{if ($1=="EID") printf $3; }' $COUFILE`
  
  rm -f $COUFILE

  case $mode in
    "DIRECTORY") return;;
  esac

  
  # Header and bins files
  for sid in $SID_LIST
  do
    sid=`expr $sid + 0`
    dir="${LOCAL_STORAGES_ROOT}_$cluster-$sid"
    doSpace="Yes"
    for file in `find $dir -name $fid`
    do
      case $doSpace in
       Yes) printf "\n"; doSpace="No";;
      esac	
      size=`ls -l $file  | awk '{ printf $5 }'`
      printf "%10s %s\n" $size $file
    done
  done     
}
do_monitor_cfg () 
{
  # Create monitor configuration file
  sid=0  
  for v in $(seq ${NB_VOLUMES}); 
  do
    echo "VOLUME ${EXPORT_HOST} $v"
    for c in $(seq ${NB_CLUSTERS_BY_VOLUME}); 
    do
      for j in $(seq ${STORAGES_BY_CLUSTER}); 
      do
        sid=$((sid+1))
        echo "STORAGE localhost$sid"
      done
    done
  done

  mount_instance=0
  NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

  for eid in $(seq ${NB_EXPORTS}); 
  do
    for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); 
    do
      echo "FSMOUNT ${EXPORT_HOST} $mount_instance"
      mount_instance=$((mount_instance+1))
    done
  done       
}
do_monitor () 
{
  case "$1" in
    "") delay="-t 10s";;
    *)  delay="-t $2";;
  esac  
  do_monitor_cfg > ${WORKING_DIR}/monitor.cfg
  ${WORKING_DIR}/monitor.py $delay -c ${WORKING_DIR}/monitor.cfg
  exit 0
}  
check_build ()
{

    if [ ! -e "${LOCAL_BINARY_DIR}/exportd/${LOCAL_EXPORT_DAEMON}" ]
    then
        echo "Daemons are not build !!! use $0 build"
        echo "${LOCAL_BINARY_DIR}/exportd/${LOCAL_EXPORT_DAEMON}"
        exit 0;
    fi

}

pjd_test()
{

    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to run pjd tests (configuration file doesn't exist)"
    else
        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`
        EXPORT_LAYOUT=`grep layout ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | grep -v grep | cut -c 10`

        for eid in $(seq ${NB_EXPORTS}); do
            echo "------------------------------------------------------"
            mountpoint -q ${LOCAL_MNT_ROOT}${eid}
            if [ "$?" -eq 0 ]
            then
                echo "Run pjd tests on ${LOCAL_MNT_PREFIX}${eid} with layout $EXPORT_LAYOUT"
                echo "------------------------------------------------------"

                cd ${LOCAL_MNT_ROOT}${eid}
                prove -r ${LOCAL_PJDTESTS}
                cd ..

            else
                echo "Unable to run pjd tests (${LOCAL_MNT_PREFIX}${eid} is not mounted)"
            fi
        done;
    fi
}

fileop_test(){

    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to run pjd tests (configuration file doesn't exist)"
    else
        LOWER_LMT=1
        UPPER_LMT=4
        INCREMENT=1
        FILE_SIZE=2M

        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`
        EXPORT_LAYOUT=`grep layout ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | grep -v grep | cut -c 10`

        for eid in $(seq ${NB_EXPORTS}); do
                echo "------------------------------------------------------"
                mountpoint -q ${LOCAL_MNT_ROOT}${eid}
                if [ "$?" -eq 0 ]
                then
                    echo "Run fileop test on ${LOCAL_MNT_PREFIX}${eid} with layout $EXPORT_LAYOUT"
                    echo "------------------------------------------------------"
                    ${FSOP_BINARY} -l ${LOWER_LMT} -u ${UPPER_LMT} -i ${INCREMENT} -e -s ${FILE_SIZE} -d ${LOCAL_MNT_ROOT}${eid}
                else
                    echo "Unable to run fileop test (${LOCAL_MNT_PREFIX}${eid} is not mounted)"
                fi
        done;
    fi
}

usage ()
{
    case $1 in
      "");;
      *) {
        echo "!!! $1 !!!"
      };;
    esac
    
    echo >&2 "Usage:"
    echo >&2 "$0 site <0|1>"
    echo >&2 "$0 start <layout>"
    echo >&2 "$0 stop"
    echo >&2 "$0 pause"
    echo >&2 "$0 resume"
    echo >&2 "$0 storage <hid>|all stop|start|reset"
    echo >&2 "$0 storage <hid>|all device-delete|device-rebuild <device>|all"
    echo >&2 "$0 storage <hid> fid-rebuild -s <cid>/<sid> -f <fid>"
    echo >&2 "$0 export stop|start|reset"
    echo >&2 "$0 fsmount stop|start|reset"
    echo >&2 "$0 geomgr modify|delete"    
    echo >&2 "$0 cou <fileName>"    
    echo >&2 "$0 core [remove] <coredir>/<corefile>"
    echo >&2 "$0 process"
    echo >&2 "$0 monitor"
    echo >&2 "$0 reload"
    echo >&2 "$0 build"
    echo >&2 "$0 rebuild"
    echo >&2 "$0 clean"
    echo >&2 "$0 pjd_test"
    echo >&2 "$0 fileop_test"
    echo >&2 "$0 mount"
    echo >&2 "$0 umount"
    exit 0
}

# $1 -> Layout to use
set_layout () {

  # Get default layout from /tmp/rozo.layout if not given as parameter
  
  case "$1" in
    "") {
      ROZOFS_LAYOUT=0
      if [ -f ${WORKING_DIR}/layout.saved ];
      then
        ROZOFS_LAYOUT=`cat ${WORKING_DIR}/layout.saved`    
      fi
    };;   
    *) ROZOFS_LAYOUT=$1;;
  esac

  case "$ROZOFS_LAYOUT" in
    0) {
      STORAGES_BY_CLUSTER=4
      NB_EXPGATEWAYS=4	
    };;
    1) {        
      STORAGES_BY_CLUSTER=8
      NB_EXPGATEWAYS=4
    };;   
    2) {
      STORAGES_BY_CLUSTER=16
      NB_EXPGATEWAYS=4
    };;
    *) {
      echo >&2 "Rozofs layout must be equal to 0, 1 or 2."
      exit 1
    };
  esac  
  # Save layout
  echo $ROZOFS_LAYOUT > ${WORKING_DIR}/layout.saved
}

display_process() {
  local header=$2
  local proc=$1
  local next=$3
  local last=0
  local idx=0
  local LIST=""
  
  LIST=`ps -ef | awk '{ if ($3==proc) print $2 ; }' proc=$proc`
  last=0
  for pid in $LIST
  do 
    last=$((last+1))
  done   
  Details=`ps -ef | awk '{ if ($2==proc) print $8" "$9" "$10" "$11" "$12" "$13" "$14" "$15" "$16" "$17; }' proc=$proc`  

  printf "%5d %s|__%s\n" $proc "$header" "$Details"
  
  idx=0
  for pid in `ps -ef | awk '{ if ($3==proc) print $2 ; }' proc=$proc`
  do 
    idx=$((idx+1))
    if [ $idx -eq $last ];
    then
      display_process $pid "${header}$next" "   "
    else
      display_process $pid "${header}$next" "|  "
    fi
  done  
}	
show_process () {

  tst_dir=`pwd | awk -F'/' '{ print $NF }'`
  LIST=`ps -ef | grep "/$tst_dir" | awk '{ if ($3==1) print $2;}'`

  for proc in $LIST
  do
    echo "_______________________________________________________________________________________"
    display_process $proc "" "   "
  done
}
main ()
{
    storaged_dir="storaged"

        
    [ $# -lt 1 ] && usage

    # to reach executables
    for dir in ${LOCAL_BUILD_DIR}/src/*
    do
      if [ -d $dir ];
      then
        export PATH=$PATH:$dir
      fi
    done  
 
    # Set new layout when given on start command
    # or read saved layout 
    if [ "$1" == "start" -a $# -ge 2 ];
    then
        # Set layout and save it
        set_layout $2  
    else
        # Read saved layout
        set_layout
    fi
    
    NB_EXPORTS=4
    # BSIZE 0=4K 1=8K 2=16K 3=32K 
    BS4K=0
    BS8K=1
    BS16K=2
    BS32K=3
    declare -a EXPORT_BSIZE=($BS4K $BS8K $BS16K $BS32K)
    declare -a EXPORT_VID=(1 1 1 1)
    NB_VOLUMES=1
    NB_CLUSTERS_BY_VOLUME=2
    NB_PORTS_PER_STORAGE_HOST=2
    NB_DISK_THREADS=3
    NB_CORES=2
    WRITE_FILE_BUFFERING_SIZE=256
    NB_STORCLI=1
    SHAPER=0
    ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS=1
    SQUOTA=""
    HQUOTA=""
    
    NB_DEVICE_PER_SID=6
    NB_DEVICE_MAPPER_PER_SID=4
    NB_DEVICE_MAPPER_RED_PER_SID=2
    # GEOREP = 1 (1 site) or 2 (2 georeplicated sites)
    GEOREP=2

    STORAGES_PER_SITE=$((NB_VOLUMES*NB_CLUSTERS_BY_VOLUME*STORAGES_BY_CLUSTER))
    STORAGES_TOTAL=$((STORAGES_PER_SITE*GEOREP))

    # Only one storio per storage or one storio per listening port ?
    #STORIO_MODE="multiple"
    STORIO_MODE="single"   
    
    #READ_FILE_MINIMUM_SIZE=8
    READ_FILE_MINIMUM_SIZE=$WRITE_FILE_BUFFERING_SIZE
    
    EXPORT_HOST="${LOCAL_EXPORT_NAME_BASE}/192.168.36.15"

    ulimit -c unlimited
    ${WORKING_DIR}/conf_local_addr.sh set $STORAGES_TOTAL eth0 > /dev/null 2>&1 
    if [ "$1" == "site" ]
    then    
        [ $# -lt 2 ] && usage
        create_site $2   
	
    elif [ "$1" == "start" ]
    then
        check_build
        do_stop

        gen_storage_conf ${STORAGES_BY_CLUSTER} ${NB_PORTS_PER_STORAGE_HOST}
        gen_export_conf ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}
        gen_geomgr_conf
	
        go_layout ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}

        create_storages
        create_exports

        do_start_all_processes

    elif [ "$1" == "stop" ]
    then
           do_stop

    elif [ "$1" == "core" ]
    then
           do_core $*	
    elif [ "$1" == "cou" ]
    then
           do_cou $*	      
    elif [ "$1" == "pause" ]
    then
           do_pause
    elif [ "$1" == "resume" ]
    then
           do_start_all_processes

    elif [ "$1" == "reload" ]
    then

        undeploy_clients_local

        reload_storaged
        reload_exportd

        deploy_clients_local

    elif [ "$1" == "pjd_test" ]
    then
        check_build
        pjd_test
    elif [ "$1" == "fileop_test" ]
    then
        check_build
        fileop_test

    elif [ "$1" == "mount" ]
    then
        check_build
        deploy_clients_local

    elif [ "$1" == "umount" ]
    then
        check_build
        undeploy_clients_local

    elif [ "$1" == "build" ]
    then
        build

    elif [ "$1" == "rebuild" ]
    then
        rebuild
    elif [ "$1" == "export" ]
    then
      case "$2" in 
        stop)       stop_exportd;;
	start)      start_exportd;;
	reset)      reset_exportd;;	
        *)          usage;;
      esac
    elif [ "$1" == "fsmount" ]
    then
      case "$2" in 
        stop)       undeploy_clients_local;;
	start)      deploy_clients_local;;
	reset)      undeploy_clients_local;deploy_clients_local;;	
        *)          usage;;
      esac      
    elif [ "$1" == "geomgr" ]
    then
      case "$2" in 
        modify)     geomgr_modify ;;
	delete)     geomgr_delete;;	
        *)          usage;;
      esac      
    elif [ "$1" == "storage" ]
    then  	
      case "$3" in 
        stop)            stop_one_storage $2;;
	start)           start_one_storage $2;;
	device-rebuild)  rebuild_storage_device $2 $4 $5 $6;;
	device-delete)   delete_storage_device $2 $4;; 
        fid-rebuild)     rebuild_storage_fid $*;;
	reset)           reset_one_storage $2;;
        *)               usage;;
      esac
    elif [ "$1" == "storio" ]
    then  	
      case "$3" in 
        reset)           reset_one_storio $2;;
        *)               usage;;
      esac      
    elif [ "$1" == "process" ]
    then 
       show_process 
    elif [ "$1" == "monitor" ]
    then 
       set_layout
       do_monitor $2       
    elif [ "$1" == "clean" ]
    then
        clean_all
    else
        usage
    fi
    exit 0
}

main $@
