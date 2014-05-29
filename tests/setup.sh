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
process_killer () { 

  if ls /var/run/$1* > /dev/null 2>&1
  then
    for pid in `cat /var/run/$1* `
    do
      kill $pid  > /dev/null 2>&1    
    done
  else
    return  
  fi

  #sleep 2
      
  if ls /var/run/$1* > /dev/null 2>&1
  then   
    for pid in `cat /var/run/$1* `
    do
      kill -9 $pid > /dev/null 2>&1
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

    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))

    sid=0

    for i in $(seq ${nb_clusters}); do

        for j in $(seq ${STORAGES_BY_CLUSTER}); do

           sid=$((sid+1))

           FILE=${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${i}'_'${sid}'.conf'

           echo "$FILE"

            if [ ! -e "$LOCAL_CONF" ]
            then
                mkdir -p $LOCAL_CONF
            fi

            if [ -e "$FILE" ]
            then
                rm -rf $FILE
            fi

            touch $FILE

            echo "#${NAME_LABEL}" >> $FILE
            echo "#${DATE_LABEL}" >> $FILE

            printf "threads = $NB_DISK_THREADS;\n" >> $FILE
            printf "nbcores = $NB_CORES;\n" >> $FILE

            printf "listen = ( \n" >> $FILE
            printf "  {addr = \"192.168.2.$sid\"; port = 41000;}" >> $FILE

            # Test for special character "*"
            #printf "  {addr = \"*\"; port = 4100$sid;}" >> $FILE

            for idx in $(seq 2 1 ${PORT_PER_STORAGE_HOST}); do
                printf " ,\n  {addr = \"192.168.$((idx+1)).$sid\"; port = 41000; }"
            done >>  $FILE

            printf "\n);\n" >>  $FILE

            echo 'storages = (' >> $FILE
            echo "  {cid = $i; sid = $sid; root =\"${LOCAL_STORAGES_ROOT}_$i-$sid\"; device-total = $NB_DEVICE_PER_SID; device-mapper = $NB_DEVICE_MAPPER_PER_SID; device-redundancy = $NB_DEVICE_MAPPER_RED_PER_SID;}" >> $FILE
            echo ');' >> $FILE
        done; 
    done;
}

gen_storage_georep_conf ()
{
    STORAGES_BY_CLUSTER=$1
    PORT_PER_STORAGE_HOST=$2

    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))

    sid=0    
    host=$3

    for i in $(seq ${nb_clusters}); do

        for j in $(seq ${STORAGES_BY_CLUSTER}); do

           sid=$((sid+1))
           host=$((host+1))

           FILE=${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${i}'_'${host}'.conf'

           echo "$FILE"

            if [ ! -e "$LOCAL_CONF" ]
            then
                mkdir -p $LOCAL_CONF
            fi

            if [ -e "$FILE" ]
            then
                rm -rf $FILE
            fi

            touch $FILE

            echo "#${NAME_LABEL}" >> $FILE
            echo "#${DATE_LABEL}" >> $FILE

            printf "threads = $NB_DISK_THREADS;\n" >> $FILE
            printf "nbCores = $NB_CORES;\n" >> $FILE
            printf "storio  = \"$STORIO_MODE\";" >> $FILE

            printf "listen = ( \n" >> $FILE
            printf "  {addr = \"192.168.2.$host\"; port = 41000;}" >> $FILE

            # Test for special character "*"
            #printf "  {addr = \"*\"; port = 4100$sid;}" >> $FILE

            for idx in $(seq 2 1 ${PORT_PER_STORAGE_HOST}); do
                printf " ,\n  {addr = \"192.168.$((idx+1)).$host\"; port = 41000;}"
            done >>  $FILE

            printf "\n);\n" >>  $FILE
            echo 'storages = (' >> $FILE
            echo "  {cid = $i; sid = $sid; root =\"${LOCAL_STORAGES_ROOT}_$i-$host\"; device-total = $NB_DEVICE_PER_SID; device-mapper = $NB_DEVICE_MAPPER_PER_SID; device-redundancy = $NB_DEVICE_MAPPER_RED_PER_SID;}" >> $FILE
            echo ');' >> $FILE
        done; 
    done;
}
# $1 -> LAYOUT
# $2 -> storages by node
# $2 -> Nb. of exports
# $3 -> md5 generated
gen_export_gw_conf ()
{

    ROZOFS_LAYOUT=$1
    EXPORTD_VIP=$3

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
    echo "layout = ${ROZOFS_LAYOUT} ;" >> $FILE
    echo "exportd_vip = \"${EXPORTD_VIP}\" ;" >> $FILE    
    echo "nbcores = $NB_CORES;" >> $FILE
    echo 'volumes =' >> $FILE
    echo '      (' >> $FILE

        for v in $(seq ${NB_VOLUMES}); do
            echo '        {' >> $FILE
            echo "            vid = $v;" >> $FILE
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
                    if [[ ${k} == ${STORAGES_BY_CLUSTER} ]]
                    then
                        echo "                           {sid = ${sid}; host = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";}" >> $FILE
                    else
                        echo "                           {sid = ${sid}; host = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";}," >> $FILE
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
    NB_EXPORTDS=1

    echo 'export_gateways =' >> $FILE
    echo '      (' >> $FILE
        for k in $(seq ${NB_EXPORTDS}); do
            echo '        {' >> $FILE
            echo "            daemon_id = $k;" >> $FILE
            echo '            gwids= ' >> $FILE
            echo '            (' >> $FILE

                for r in $(seq ${NB_EXPGATEWAYS}); do
                    let idx=${r}-1;
                    if [[ ${r} == ${NB_EXPGATEWAYS} ]]
                    then
                        echo "                 {gwid = ${r}; host = \"${LOCAL_STORAGE_NAME_BASE}${r}\";}" >> $FILE
                    else
                        echo "                 {gwid = ${r}; host = \"${LOCAL_STORAGE_NAME_BASE}${r}\";}," >> $FILE
                    fi
                done;
            echo '              );' >> $FILE
        if [[ ${k} == ${NB_EXPORTDS} ]]
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
            echo "   {eid = $k; root = \"${LOCAL_EXPORTS_ROOT}_$k\"; md5=\"${4}\"; squota=\"$SQUOTA\"; hquota=\"$HQUOTA\"; vid=${k};}" >> $FILE
        else
            echo "   {eid = $k; root = \"${LOCAL_EXPORTS_ROOT}_$k\"; md5=\"${4}\"; squota=\"$SQUOTA\"; hquota=\"$HQUOTA\"; vid=${k};}," >> $FILE
        fi
    done;
    echo ');' >> $FILE
}

# $1 -> LAYOUT
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
                    if [[ ${k} == ${STORAGES_BY_CLUSTER} ]]
                    then
                        echo "                           {sid = ${sid}; host = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";}" >> $FILE
                    else
                        echo "                           {sid = ${sid}; host = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";}," >> $FILE
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

# $1 -> LAYOUT
gen_export_georep_conf ()
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
	    echo "            georep = 1;" >> $FILE
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
		    sid_geo=$((sid+8))
                    if [[ ${k} == ${STORAGES_BY_CLUSTER} ]]
                    then
                        echo "                           {sid = ${sid}; site0 = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";site1 = \"${LOCAL_STORAGE_NAME_BASE}${sid_geo}\";}" >> $FILE
                    else
                        echo "                           {sid = ${sid}; site0 = \"${LOCAL_STORAGE_NAME_BASE}${sid}\";site1 = \"${LOCAL_STORAGE_NAME_BASE}${sid_geo}\";}," >> $FILE
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

start_one_storage() 
{
	case $1 in
		"all") start_storaged ${STORAGES_BY_CLUSTER} 0; return;;
	esac
   
	sid=$1
	if [ $1 -gt 8 ];
	then 
	 cid=$(( ((sid-9) / STORAGES_BY_CLUSTER) + 1 ))
	else
	 cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))
	fi
	#echo "Start storage cid: $cid sid: $sid"
	${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_DAEMON} -c ${LOCAL_CONF}'_'$cid'_'$sid"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}$sid
	#sleep 1
}
rebuild_storage_fid() 
{
    sid=$2
    cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))
    shift 3

    echo "${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_REBUILD} -c ${LOCAL_CONF}'_'$cid'_'$sid"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}$sid -r localhost $*"
        
    ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_REBUILD} -c ${LOCAL_CONF}'_'$cid'_'$sid"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}$sid -r localhost $*
}
rebuild_storage_device() 
{

    sid=$1
    cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))
    case "$2" in
      "")   usage "Missing device identifier";;
      all)  dev="";;
      *)    dev="-d $2";;
    esac

    echo "rebuild $cid/$sid device $2" 
    
    create_storage_device $1 $2
    
    ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_REBUILD} -c ${LOCAL_CONF}'_'$cid'_'$sid"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}$sid -r localhost --sid $cid/$sid $dev
}
delete_storage_device() 
{
    sid=$1
    cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))
    case "$2" in
      "")   usage "Missing device identifier";;
      all)  begin=0  ; end=$((NB_DEVICE_PER_SID-1));;
      *)    begin=$2 ; end=$2;;
    esac
    for device in $(seq $begin $end)
    do

      dir="${LOCAL_STORAGES_ROOT}_$cid-$sid/$device"
      if [ -d $dir ];
      then
        echo "delete $cid/$sid device $device : $dir" 
        \rm -rf $dir
      else
        echo "$dir does not exist !!!"         	  
      fi
    done  
}
create_storage_device() 
{
    sid=$1
    if [ $sid -ge 9 ];
    then
      cid=$(( ((sid-9) / STORAGES_BY_CLUSTER) + 1 ))
    else
      cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))
    fi  
    case "$2" in
      "")  begin=0  ; end=$((NB_DEVICE_PER_SID-1));;
      all) begin=0  ; end=$((NB_DEVICE_PER_SID-1));;
      *)   begin=$2 ; end=$2;;
    esac
    for device in $(seq $begin $end)
    do
      mkdir ${LOCAL_STORAGES_ROOT}_$cid-$sid/$device > /dev/null 2>&1
    done  
}
stop_one_storage () {
   case $1 in
     "all") stop_storaged; return;;
   esac
   
   process_killer "storaged_${LOCAL_STORAGE_NAME_BASE}$1."
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

    sid=$2
    
    for v in $(seq ${NB_VOLUMES}); do
        for c in $(seq ${NB_CLUSTERS_BY_VOLUME}); do
	   for j in $(seq ${STORAGES_BY_CLUSTER}); do
	      sid=$((sid+1))
              echo "start storaged" ${LOCAL_CONF}'_'${c}'_'${sid}"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}${sid}
echo ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_DAEMON}
              ${LOCAL_BINARY_DIR}/$storaged_dir/${LOCAL_STORAGE_DAEMON} -c ${LOCAL_CONF}'_'${c}'_'${sid}"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}${sid}
           done
	done
    done
}

stop_storaged()
{
   echo "------------------------------------------------------"
   echo "Stopping the storaged"
   sid=0
   sid_geo=8
    
   for v in $(seq ${NB_VOLUMES}); do
     for c in $(seq ${NB_CLUSTERS_BY_VOLUME}); do
       for j in $(seq ${STORAGES_BY_CLUSTER}); do
	 sid=$((sid+1))
	 sid_geo=$((sid_geo+1))
	 stop_one_storage $sid
	 stop_one_storage $sid_geo
       done
    done
  done   
}
reload_storaged ()
{
    echo "------------------------------------------------------"
    echo "Reload ${LOCAL_STORAGE_DAEMON}"
    kill -1 `ps ax | grep ${LOCAL_STORAGE_DAEMON} | grep -v grep | awk '{print $1}'`
}

# $1 -> starting sid
create_storages ()
{

    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))

    sid=$1
     
    for i in $(seq ${nb_clusters}); do

        for j in $(seq ${STORAGES_BY_CLUSTER}); do

            sid=$((sid+1))
	    
            if [ -e "${LOCAL_STORAGES_ROOT}_${i}-${sid}" ]
            then
                rm -rf ${LOCAL_STORAGES_ROOT}_${i}-${sid}/*.bins
            else
                mkdir -p ${LOCAL_STORAGES_ROOT}_${i}-${sid}
            fi
	    
	    create_storage_device $sid all
	    
        done;

    done;
}

# $1 -> storages by node
remove_storages ()
{
    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))

    sid=0
    
    for i in $(seq ${nb_clusters}); do

        for j in $(seq ${STORAGES_BY_CLUSTER}); do

            sid=$((sid+1))

            if [ -e "${LOCAL_STORAGES_ROOT}_${i}-${sid}" ]
            then
                rm -rf ${LOCAL_STORAGES_ROOT}_${i}-${sid}
            fi

        done;

    done;
}
start_one_expgw ()
{

  if [ ! -f   ${LOCAL_BINARY_DIR}/exportd/expgateway ];
  then
    return
  fi
  
  case $1 in
    "all") start_expgw; return;;
  esac  
  
  host=${LOCAL_STORAGE_NAME_BASE}$1   
   
  echo "start export gateway $host"
  ${LOCAL_BINARY_DIR}/exportd/expgateway  -L $host -P 60000 &
}
start_expgw ()
{
    
   echo "Start Export Gateway(s)"
   for j in $(seq ${NB_EXPGATEWAYS}); 
   do
      start_one_expgw $j
   done
}
stop_one_expgw () {

  if [ ! -f   ${LOCAL_BINARY_DIR}/exportd/expgateway ];
  then
    return
  fi
  
   case $1 in
     "all") stop_expgw; return;;
   esac  

   process_killer expgw_${LOCAL_STORAGE_NAME_BASE}$1 
} 
reset_one_expgw () {  
  stop_one_expgw  $1
  start_one_expgw $1 
}
stop_expgw () {
    echo "------------------------------------------------------"
    echo "Killing export gateway"
    process_killer expgw
}
# $1 -> LAYOUT
# $2 -> NB STORAGES BY CLUSTER
go_layout ()
{
    ROZOFS_LAYOUT=$1
    STORAGES_BY_CLUSTER=$2

    if [ ! -e "${LOCAL_CONF}export_l${ROZOFS_LAYOUT}.conf" ] || [ ! -e "${LOCAL_CONF}export_l${ROZOFS_LAYOUT}.conf" ]
    then
        echo "Unable to change configuration files to layout ${ROZOFS_LAYOUT}"
        exit 0
    else
        ln -s -f ${LOCAL_CONF}'export_l'${ROZOFS_LAYOUT}'.conf' ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}


        let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))
        sid=0
    
	for i in $(seq ${nb_clusters}); do
            for j in $(seq ${STORAGES_BY_CLUSTER}); do
	    
        	sid=$((sid+1))
                ln -s -f ${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${i}'_'${sid}'.conf' ${LOCAL_CONF}'_'${i}'_'${sid}"_"${LOCAL_STORAGE_CONF_FILE}
            done;
	done;
    fi
}
go_layout_georep ()
{
    ROZOFS_LAYOUT=$1
    STORAGES_BY_CLUSTER=$2

    if [ ! -e "${LOCAL_CONF}export_l${ROZOFS_LAYOUT}.conf" ] || [ ! -e "${LOCAL_CONF}export_l${ROZOFS_LAYOUT}.conf" ]
    then
        echo "Unable to change configuration files to layout ${ROZOFS_LAYOUT}"
        exit 0
    else
        ln -s -f ${LOCAL_CONF}'export_l'${ROZOFS_LAYOUT}'.conf' ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}


        let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))
        sid=0
    
	for i in $(seq ${nb_clusters}); do
            for j in $(seq ${STORAGES_BY_CLUSTER}); do
	    
        	sid=$((sid+1))
                ln -s -f ${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${i}'_'${sid}'.conf' ${LOCAL_CONF}'_'${i}'_'${sid}"_"${LOCAL_STORAGE_CONF_FILE}
            done;
	done;


        let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))
        sid=8
    
	for i in $(seq ${nb_clusters}); do
            for j in $(seq ${STORAGES_BY_CLUSTER}); do
	    
        	sid=$((sid+1))
                ln -s -f ${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${i}'_'${sid}'.conf' ${LOCAL_CONF}'_'${i}'_'${sid}"_"${LOCAL_STORAGE_CONF_FILE}
            done;
	done;
    fi
}

deploy_clients_local ()
{
    mount_instance=0
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to mount RozoFS (configuration file doesn't exist)"
    else

        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        for j in $(seq ${NB_EXPORTS}); do


            for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do

                option=""
                mountpoint -q ${LOCAL_MNT_ROOT}${j}_${idx_client}

                if [ "$?" -ne 0 ]
                then

                    echo "Mount RozoFS (export: ${LOCAL_EXPORTS_NAME_PREFIX}_${j}) on ${LOCAL_MNT_PREFIX}${j}_${idx_client}"

                    if [ ! -e "${LOCAL_MNT_ROOT}${j}_${idx_client}" ]
                    then
                        mkdir -p ${LOCAL_MNT_ROOT}${j}_${idx_client}
                    fi

                    option=" -o rozofsexporttimeout=24 -o rozofsstoragetimeout=4 -o rozofsstorclitimeout=11"
                    option="$option -o nbcores=$NB_CORES"
                    option="$option -o rozofsbufsize=$WRITE_FILE_BUFFERING_SIZE -o rozofsminreadsize=$READ_FILE_MINIMUM_SIZE" 
                    option="$option -o rozofsnbstorcli=$NB_STORCLI"
                    option="$option -o rozofsshaper=$SHAPER"
                    option="$option -o posixlock"
                    option="$option -o bsdlock"
                    option="$option -o instance=$mount_instance"
		    
                    mount_instance=$((mount_instance+1))

                    echo ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} \
                            ${LOCAL_MNT_ROOT}${j}_${idx_client} ${option}

                    ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} ${LOCAL_MNT_ROOT}${j}_${idx_client} ${option}
                    #${LOCAL_BINARY_DIR}/storcli/${LOCAL_ROZOFS_STORCLI} -i 1 -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${LOCAL_MNT_ROOT}${j}_${idx_client}  -D 610${j1&
                    #${LOCAL_BINARY_DIR}/storcli/${LOCAL_ROZOFS_STORCLI} -i 2 -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${LOCAL_MNT_ROOT}${j}_${idx_client} -D 610${j}2&
                    
                else
                    echo "Unable to mount RozoFS (${LOCAL_MNT_PREFIX}_${j}_${idx_client} already mounted)"
                fi

            done;
        done;
    fi
}
deploy_clients_local_geo_bis ()
{

    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to mount RozoFS (configuration file doesn't exist)"
    else

        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        let "mount_instance=${NB_EXPORTS}*${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}*${1}"
	echo "starting mount instance $mount_instance"
        for j in $(seq ${NB_EXPORTS}); do	    

            for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do
                mount_point=${LOCAL_MNT_ROOT}${j}_${idx_client}_g${1}
                mountpoint -q ${mount_point}
	

                if [ "$?" -ne 0 ]
                then

                    echo "Mount RozoFS (export: ${LOCAL_EXPORTS_NAME_PREFIX}_${j}) on ${mount_point}"

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
		    option="$option -o site=${1}"	    
                    option="$option -o instance=$mount_instance"


                    echo ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} \
                            ${mount_point} ${option}

                    ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} ${mount_point} ${option}
                     echo ${LOCAL_BINARY_DIR}/geocli/geocli -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${mount_point} -G $1 -i $mount_instance

                    ${LOCAL_BINARY_DIR}/geocli/geocli -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${mount_point} -G $1 -i $mount_instance &        
                    mount_instance=$((mount_instance+1))
                else
                    echo "Unable to mount RozoFS (${mount_point} already mounted)"
                fi

            done;
        done;
    fi
}

deploy_clients_local_geo_ter ()
{

    nb_site=2
    mount_instance=0
    
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to mount RozoFS (configuration file doesn't exist)"
    else

        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        for j in $(seq ${NB_EXPORTS}); do	    

            for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do
	        for idx_site in  $(seq 0 1); do
                  mount_point=${LOCAL_MNT_ROOT}${j}_${idx_client}_g${idx_site}
                  mountpoint -q ${mount_point}


                  if [ "$?" -ne 0 ]
                  then

                      echo "Mount RozoFS (export: ${LOCAL_EXPORTS_NAME_PREFIX}_${j}) on ${mount_point}"

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
		      option="$option -o site=${idx_site}"	    
                      option="$option -o instance=$mount_instance"


                      echo ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} \
                              ${mount_point} ${option}

                      ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} ${mount_point} ${option}
                       echo ${LOCAL_BINARY_DIR}/geocli/geocli -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${mount_point} -G $idx_site -i $mount_instance

                      ${LOCAL_BINARY_DIR}/geocli/geocli -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${mount_point} -G $idx_site -i $mount_instance &        
                      mount_instance=$((mount_instance+1))
                  else
                      echo "Unable to mount RozoFS (${mount_point} already mounted)"
                  fi
              done;
            done;
        done;
    fi
}
deploy_clients_local_geo ()
{
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to mount RozoFS (configuration file doesn't exist)"
    else

        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        for j in $(seq ${NB_EXPORTS}); do
            geo_site=0
	    

            for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do
                mount_point=${LOCAL_MNT_ROOT}${j}_${idx_client}_g${geo_site}
                mountpoint -q ${mount_point}
	

                if [ "$?" -ne 0 ]
                then

                    echo "Mount RozoFS (export: ${LOCAL_EXPORTS_NAME_PREFIX}_${j}) on ${mount_point}"

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
		    option="$option -o site=${geo_site}"	    
                    let "INSTANCE=${idx_client}-1"
		    INSTANCE=$((2*INSTANCE))
                    option="$option -o instance=$INSTANCE"

                    echo ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} \
                            ${mount_point} ${option}

                    ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} ${mount_point} ${option}
                    INSTANCE=$((INSTANCE+1))
                     echo ${LOCAL_BINARY_DIR}/geocli/geocli -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${mount_point} -G $geo_site -i $INSTANCE

                    ${LOCAL_BINARY_DIR}/geocli/geocli -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${mount_point} -G $geo_site -i $INSTANCE &        

 		    geo_site=$((1-geo_site))
                   
                else
                    echo "Unable to mount RozoFS (${mount_point} already mounted)"
                fi

            done;
        done;
    fi
}
rozofsmount_kill_best_effort()
{
    #echo "------------------------------------------------------"
    echo "Killing rozofsmount and storcli in best effort mode"
    process_killer rozofsmount
}
geocli_kill_best_effort()
{
    #echo "------------------------------------------------------"
    echo "Killing geocli and storcli in best effort mode"
    process_killer geocli
}
undeploy_clients_local ()
{
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
        then
        echo "Unable to umount RozoFS (configuration file doesn't exist)"
        storcli_killer.sh 
    else

        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        # Kill every storcli process
        #for pid in `ps -ef | grep storcli | grep ${LOCAL_EXPORT_NAME_BASE} | awk '{print $2 }'`
        #do
        #   kill -9 $pid
        #done

        for j in $(seq ${NB_EXPORTS}); do
            geo_site=0
            for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); do
	        for geo_site in $(seq 0 1); do
                mount_point=${LOCAL_MNT_ROOT}${j}_${idx_client}_g${geo_site}
                echo "Umount RozoFS mnt: ${LOCAL_MNT_PREFIX}${j}_${idx_client}"

                umount ${LOCAL_MNT_ROOT}${j}_${idx_client}
		case $? in
		  0) ;;
		  *) umount -l ${LOCAL_MNT_ROOT}${j}_${idx_client};;
		esac  

                rm -rf ${LOCAL_MNT_ROOT}${j}_${idx_client}

                umount ${mount_point}
		case $? in
		  0) ;;
		  *) umount -l $mount_point;;
		esac  

                rm -rf $mount_point
                storcli_killer.sh $mount_point > /dev/null 2>&1

               done
	    done

        done

    sleep 0.4

    rozofsmount_kill_best_effort
    geocli_kill_best_effort
    fi
}

start_exportd ()
{
    echo "------------------------------------------------------"
    echo "Start ${LOCAL_EXPORT_DAEMON}"
    ${LOCAL_BINARY_DIR}/exportd/${LOCAL_EXPORT_DAEMON} -c ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}

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
    else
        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        for k in $(seq ${NB_EXPORTS}); do
            if [ -e "${LOCAL_EXPORTS_ROOT}_${k}" ]
            then
                rm -rf ${LOCAL_EXPORTS_ROOT}_${k}/*
            else
                mkdir -p ${LOCAL_EXPORTS_ROOT}_${k}
            fi
        done;
    fi
}

# $1 -> Nb. of exports
remove_exports ()
{
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
    then
        echo "Unable to remove export directories (configuration file doesn't exist)"
    else
        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        for j in $(seq ${NB_EXPORTS}); do

            if [ -e "${LOCAL_EXPORTS_ROOT}_${j}" ]
            then
                rm -rf ${LOCAL_EXPORTS_ROOT}_${j}
            fi
        done;
    fi
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
     start_storaged ${STORAGES_BY_CLUSTER} 0
     #start_expgw
     start_exportd 1
     sleep 2
     deploy_clients_local
}

do_start_all_processes_geo() {
     start_storaged ${STORAGES_BY_CLUSTER} 0
     start_storaged ${STORAGES_BY_CLUSTER} 8
     #start_expgw
     start_exportd 1
     deploy_clients_local_geo_ter
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
  expgw)           bin=${LOCAL_BINARY_DIR}/exportd/$1;;
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
    echo "VOLUME localhost $v"
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

  for j in $(seq ${NB_EXPORTS}); 
  do
    for idx_client in $(seq ${ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS}); 
    do
      echo "FSMOUNT localhost $mount_instance"
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

        for j in $(seq ${NB_EXPORTS}); do
            echo "------------------------------------------------------"
            mountpoint -q ${LOCAL_MNT_ROOT}${j}
            if [ "$?" -eq 0 ]
            then
                echo "Run pjd tests on ${LOCAL_MNT_PREFIX}${j} with layout $EXPORT_LAYOUT"
                echo "------------------------------------------------------"

                cd ${LOCAL_MNT_ROOT}${j}
                prove -r ${LOCAL_PJDTESTS}
                cd ..

            else
                echo "Unable to run pjd tests (${LOCAL_MNT_PREFIX}${j} is not mounted)"
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

        for j in $(seq ${NB_EXPORTS}); do
                echo "------------------------------------------------------"
                mountpoint -q ${LOCAL_MNT_ROOT}${j}
                if [ "$?" -eq 0 ]
                then
                    echo "Run fileop test on ${LOCAL_MNT_PREFIX}${j} with layout $EXPORT_LAYOUT"
                    echo "------------------------------------------------------"
                    ${FSOP_BINARY} -l ${LOWER_LMT} -u ${UPPER_LMT} -i ${INCREMENT} -e -s ${FILE_SIZE} -d ${LOCAL_MNT_ROOT}${j}
                else
                    echo "Unable to run fileop test (${LOCAL_MNT_PREFIX}${j} is not mounted)"
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
    echo >&2 "$0 storage <sid>|all stop|start|reset"
    echo >&2 "$0 storage <sid>|all device-delete|device-rebuild <device>|all"
    echo >&2 "$0 storage <sid> fid-rebuild -s <cid>/<sid> -f <fid>"
    echo >&2 "$0 expgw <nb>|all stop|start|reset"
    echo >&2 "$0 export stop|start|reset"
    echo >&2 "$0 fsmount stop|start|reset"
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
  ROZOFS_LAYOUT=$1
  case "$ROZOFS_LAYOUT" in
    "") ROZOFS_LAYOUT=`cat ${WORKING_DIR}/layout.saved`
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
	
show_process () {
  cd /var/run
  LIST=""
  
  file=exportd.pid
  if [ -f $file ];
  then
    proc=`cat $file`
    printf "\n[export:%d]\n" $proc
  else
    printf "\n[export:--]\n" 
  fi
  
  for file in expgw_*.pid
  do
    if [ -f $file ];
    then
      proc=`cat $file`
      name=`echo $file | awk -F':' '{print $1}'`
      nb=`echo ${name: -1}`
      printf "[expgw %d:%d] " $nb $proc
    fi    
  done  
  printf "\n"
  printf " cid sid storaged     storio(s)\n"
  for sid in $(seq $nbaddr)
  do
  
    cid=$(( ((sid-1) / STORAGES_BY_CLUSTER) + 1 ))  
    std=storaged_${LOCAL_STORAGE_NAME_BASE}$sid

    printf " %3d %3d " $cid $sid

    file=storaged_${LOCAL_STORAGE_NAME_BASE}$sid.pid
    if [ -f $file ];
    then
      proc=`cat $file`
      printf " %6d     " $proc 
    else
      printf "     --      "         
    fi 
    
    if ls storio_${LOCAL_STORAGE_NAME_BASE}$sid.*.pid > /dev/null 2>&1
    then

      for file in storio_${LOCAL_STORAGE_NAME_BASE}$sid.*.pid
      do
        nb=`echo $file | awk -F'.' '{print $2}'`
	    proc=`cat $file`
	    printf " %d:%-6d " $nb $proc 
      done
    fi  
    printf "\n"          
  done
  
  # Clients 
  echo ""
  printf "\nClients:\n"
  for file in rozofsmount_*
  do
    if [ -f $file ];
    then
      proc=`cat $file`
      first=`echo $file | awk -F'.' '{print $1}'`
      last=`echo $file | awk -F'.' '{print $NF}'`
      if [ $first == $last ];
      then
        name=$first
      else
        name="$first $last"
      fi		
      printf "  %-23s %5d\n" "$name" $proc
    fi    
  done  
  printf "\n"
  cd - 
}
main ()
{
    storaged_dir="storaged"

        
    [ $# -lt 1 ] && usage

    # to reach storcli executable
    export PATH=$PATH:${LOCAL_BUILD_DIR}/src/storcli
    # to reach storcli_starter.sh  
    export PATH=$PATH:${LOCAL_SOURCE_DIR}/src/rozofsmount
    # to reach storcli_starter.sh  
    export PATH=$PATH:${LOCAL_SOURCE_DIR}/src/geocli
    # to reach storio executable
    export PATH=$PATH:${LOCAL_BUILD_DIR}/src/$storaged_dir
    # to reach storio_starter.sh  
    export PATH=$PATH:${LOCAL_SOURCE_DIR}/src/$storaged_dir
    # to reach exports_starter.sh  
    export PATH=$PATH:${LOCAL_SOURCE_DIR}/src/exportd
    # to reach exportd slave  
    export PATH=$PATH:${LOCAL_BUILD_DIR}/src/exportd    
    
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
    


    NB_EXPORTS=2
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
    NB_CORES=4
    WRITE_FILE_BUFFERING_SIZE=256
    NB_STORCLI=1
    SHAPER=0
    ROZOFSMOUNT_CLIENT_NB_BY_EXPORT_FS=2
    SQUOTA=""
    HQUOTA=""
    
    NB_DEVICE_PER_SID=6
    NB_DEVICE_MAPPER_PER_SID=4
    NB_DEVICE_MAPPER_RED_PER_SID=2
    GEOREP=2

    # Only one storio per storage or one storio per listening port ?
    #MULTIIO="-m"
    MULTIIO=""   
    
    #READ_FILE_MINIMUM_SIZE=8
    READ_FILE_MINIMUM_SIZE=$WRITE_FILE_BUFFERING_SIZE

    ulimit -c unlimited
    nbaddr=$((STORAGES_BY_CLUSTER*NB_CLUSTERS_BY_VOLUME*NB_VOLUMES*GEOREP))
    ${WORKING_DIR}/conf_local_addr.sh set $nbaddr eth0 > /dev/null 2>&1 
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

        go_layout ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}

        create_storages 0
        create_exports

        do_start_all_processes
    elif [ "$1" == "startg" ]
    then

        [ $# -lt 2 ] && usage

        # Set layout
        set_layout $2

        check_build
        do_stop

        gen_storage_georep_conf ${STORAGES_BY_CLUSTER} ${NB_PORTS_PER_STORAGE_HOST} 0
        gen_storage_georep_conf ${STORAGES_BY_CLUSTER} ${NB_PORTS_PER_STORAGE_HOST} 8
        gen_export_georep_conf ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}

        go_layout_georep ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}

        create_storages 0
        create_storages 8
        create_exports

        do_start_all_processes_geo

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
    elif [ "$1" == "expgw" ]
    then
      case "$3" in 
        stop)       stop_one_expgw $2;;
	start)      start_one_expgw $2;;
	reset)      reset_one_expgw $2;;	
        *)          usage;;
      esac
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
    elif [ "$1" == "storage" ]
    then  	
      case "$3" in 
        stop)            stop_one_storage $2;;
	start)           start_one_storage $2;;
	device-rebuild)  rebuild_storage_device $2 $4;;
	device-delete)   delete_storage_device $2 $4;; 
        fid-rebuild)     rebuild_storage_fid $*;;
	reset)           reset_one_storage $2;;
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
