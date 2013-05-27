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

. env.sh

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


# $1 -> LAYOUT
# $2 -> Number or serving port per storage host (nb of process)
gen_storage_conf ()
{
    STORAGES_BY_CLUSTER=$1
    PORT_PER_STORAGE_HOST=$2


    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))


	for i in $(seq ${nb_clusters}); do

		for j in $(seq ${STORAGES_BY_CLUSTER}); do
           FILE=${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${j}'.conf'
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
           #echo "ports = [ 51000, 51001] ;" >> $FILE
	   PORT_LIST="51001"
	   for portIdx in $(seq 2 1 ${PORT_PER_STORAGE_HOST}); do
	     PORT_LIST=`echo "${PORT_LIST}, 5100${portIdx}"`
	   done
	   echo "ports = [ ${PORT_LIST}] ;" >> $FILE	   
           echo 'storages = (' >> $FILE
		   for z in $(seq ${STORAGES_BY_CLUSTER}); do
		    if [[ ${i} == ${nb_clusters} && ${z} == ${STORAGES_BY_CLUSTER} ]]
		    then
		        echo "  {cid = $i; sid = $z; root =\"${LOCAL_STORAGES_ROOT}_$i-$z\";}" >> $FILE
		    else
		        echo "  {cid = $i; sid = $z; root =\"${LOCAL_STORAGES_ROOT}_$i-$z\";}," >> $FILE
		    fi
            
            done;
            echo ');' >> $FILE

		done;

	done;



}

# $1 -> LAYOUT
# $2 -> storages by node
# $2 -> Nb. of exports
# $3 -> md5 generated
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

    touch $FILE
    echo "#${NAME_LABEL}" >> $FILE
    echo "#${DATE_LABEL}" >> $FILE
    echo "layout = ${ROZOFS_LAYOUT} ;" >> $FILE
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
                    let idx=${k}-1;
                     idx_tmp_1=$(((${v}-1)*${NB_CLUSTERS_BY_VOLUME}*${STORAGES_BY_CLUSTER}))
                     idx_tmp_2=$((${STORAGES_BY_CLUSTER}*(${c}-1)))
                    if [[ ${k} == ${STORAGES_BY_CLUSTER} ]]
                    then
                        echo "                           {sid = ${k}; host = \"${LOCAL_STORAGE_NAME_BASE}${k}\";}" >> $FILE
                    else
                        echo "                           {sid = ${k}; host = \"${LOCAL_STORAGE_NAME_BASE}${k}\";}," >> $FILE
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
            echo "   {eid = $k; root = \"${LOCAL_EXPORTS_ROOT}_$k\"; md5=\"${3}\"; squota=\"\"; hquota=\"\"; vid=${k};}" >> $FILE
        else
            echo "   {eid = $k; root = \"${LOCAL_EXPORTS_ROOT}_$k\"; md5=\"${3}\"; squota=\"\"; hquota=\"\"; vid=${k};}," >> $FILE
        fi
    done;
    echo ');' >> $FILE
}

# $1 = STORAGES_BY_CLUSTER
start_storaged ()
{
    STORAGES_BY_CLUSTER=$1
    
    echo "------------------------------------------------------"

    echo "Start ${LOCAL_STORAGE_DAEMON}"
	for j in $(seq ${STORAGES_BY_CLUSTER}); do
    echo "start storaged" ${LOCAL_CONF}'_'${j}"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}${j}
    ${LOCAL_BINARY_DIR}/storaged/${LOCAL_STORAGE_DAEMON} -c ${LOCAL_CONF}'_'${j}"_"${LOCAL_STORAGE_CONF_FILE} -H ${LOCAL_STORAGE_NAME_BASE}${j}
    done
}

stop_storaged()
{
   echo "Stopping the storaged"
   for pid in `cat /var/run/storaged*.pid`
   do
     kill  $pid
   done
   sleep 1
   for pid in `cat /var/run/storaged*`
   do
     kill -9 $pid
   done
}

stop_storaged_old ()
{
    echo "------------------------------------------------------"
    PID=`ps ax | grep ${LOCAL_STORAGE_DAEMON} | grep -v grep | awk '{print $1}'`
    if [ "$PID" != "" ]
    then
        echo "Stop ${LOCAL_STORAGE_DAEMON} (PID: ${PID})"
        kill $PID
    else
        echo "Unable to stop ${LOCAL_STORAGE_DAEMON} (not running)"
    fi
}

reload_storaged ()
{
    echo "------------------------------------------------------"
    echo "Reload ${LOCAL_STORAGE_DAEMON}"
    kill -1 `ps ax | grep ${LOCAL_STORAGE_DAEMON} | grep -v grep | awk '{print $1}'`
}

# $1 -> storages by node
create_storages ()
{

    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))

    for i in $(seq ${nb_clusters}); do

        for j in $(seq ${STORAGES_BY_CLUSTER}); do

            if [ -e "${LOCAL_STORAGES_ROOT}_${i}-${j}" ]
            then
                rm -rf ${LOCAL_STORAGES_ROOT}_${i}-${j}/*.bins
            else
                mkdir -p ${LOCAL_STORAGES_ROOT}_${i}-${j}
            fi

        done;

    done;
}

# $1 -> storages by node
remove_storages ()
{
    let nb_clusters=$((${NB_CLUSTERS_BY_VOLUME}*${NB_VOLUMES}))

    for i in $(seq ${nb_clusters}); do

        for j in $(seq ${STORAGES_BY_CLUSTER}); do

            if [ -e "${LOCAL_STORAGES_ROOT}_${i}-${j}" ]
            then
                rm -rf ${LOCAL_STORAGES_ROOT}_${i}-${j}
            fi

        done;

    done;
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

	for j in $(seq ${STORAGES_BY_CLUSTER}); do

            ln -s -f ${LOCAL_CONF}'storage_l'${ROZOFS_LAYOUT}'_'${j}'.conf' ${LOCAL_CONF}'_'${j}"_"${LOCAL_STORAGE_CONF_FILE}

        done
    fi
}

deploy_clients_local ()
{
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
        then
        echo "Unable to mount RozoFS (configuration file doesn't exist)"
    else
        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`

        for j in $(seq ${NB_EXPORTS}); do
            mountpoint -q ${LOCAL_MNT_ROOT}${j}
            if [ "$?" -ne 0 ]
            then
                echo "Mount RozoFS (export: ${LOCAL_EXPORTS_NAME_PREFIX}_${j}) on ${LOCAL_MNT_PREFIX}${j}"

                if [ ! -e "${LOCAL_MNT_ROOT}${j}" ]
                then
                    mkdir -p ${LOCAL_MNT_ROOT}${j}
                fi
#                option="-o debug_port=610${j}0 -o instance=1 -o rozofsstorclitimeout=11 -o rozofsexporttimeout=24 -o rozofsstoragetimeout=4" 
               option=" -o rozofsexporttimeout=24 -o rozofsstoragetimeout=4 -o rozofsstorclitimeout=11" 
                
echo ${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} ${LOCAL_MNT_ROOT}${j} ${option}
${LOCAL_BINARY_DIR}/rozofsmount/${LOCAL_ROZOFS_CLIENT} -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j} ${LOCAL_MNT_ROOT}${j} ${option}
#                ${LOCAL_BINARY_DIR}/storcli/${LOCAL_ROZOFS_STORCLI} -i 1 -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${LOCAL_MNT_ROOT}${j}  -D 610${j}1& 
#                ${LOCAL_BINARY_DIR}/storcli/${LOCAL_ROZOFS_STORCLI} -i 2 -H ${LOCAL_EXPORT_NAME_BASE} -E ${LOCAL_EXPORTS_ROOT}_${j}  -M ${LOCAL_MNT_ROOT}${j} -D 610${j}2&
                
            else
                echo "Unable to mount RozoFS (${LOCAL_MNT_PREFIX}_${j} already mounted)"
            fi
        done;
    fi
}

rozofsmount_kill_best_effort()
{
    echo "------------------------------------------------------"
    echo "Killing rozofsmount and storcli in best effort mode"
    for pid in `cat /var/run/rozofsmount*`
    do
      kill  $pid
    done
    sleep 1
    for pid in `cat /var/run/rozofsmount*`
    do
      kill -9 $pid
    done


}

undeploy_clients_local ()
{
    echo "------------------------------------------------------"
    if [ ! -e "${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE}" ]
        then
        echo "Unable to umount RozoFS (configuration file doesn't exist)"
    else
        NB_EXPORTS=`grep eid ${LOCAL_CONF}${LOCAL_EXPORT_CONF_FILE} | wc -l`
	# Kill every storcli process
#	for pid in `ps -ef | grep storcli | grep ${LOCAL_EXPORT_NAME_BASE} | awk '{print $2 }'`
#	do
#	   kill -9 $pid
#	done
        for j in $(seq ${NB_EXPORTS}); do
            echo "Umount RozoFS mnt: ${LOCAL_MNT_PREFIX}${j}"
            umount ${LOCAL_MNT_ROOT}${j}
            test -d ${LOCAL_MNT_ROOT}${j} && rm -rf ${LOCAL_MNT_ROOT}${j}
            test -d ${LOCAL_MNT_ROOT}${j} && umount -l ${LOCAL_MNT_ROOT}${j}
            test -d ${LOCAL_MNT_ROOT}${j} && storcli_killer.sh ${LOCAL_MNT_ROOT}${j}
        done
    sleep 2
    rozofsmount_kill_best_effort
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
    for pid in `cat /var/run/export*.pid`
    do
      kill  $pid
    done
    sleep 1
    for pid in `cat /var/run/export*.pid`
    do
      kill -9 $pid
    done
}
stop_exportd_old ()
{
    echo "------------------------------------------------------"
    PID=`ps ax | grep ${LOCAL_EXPORT_DAEMON} | grep -v grep | awk '{print $1}'`
    if [ "$PID" != "" ]
    then
        echo "Stop ${LOCAL_EXPORT_DAEMON} (PID: ${PID})"
        kill $PID
    else
        echo "Unable to stop ${LOCAL_EXPORT_DAEMON} (not running)"
    fi
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

clean_all ()
{
    undeploy_clients_local
    stop_storaged
    stop_exportd
    remove_build
    remove_all
}

check_no_run_old ()
{

    PID_EXPORTD=`ps ax | grep ${LOCAL_EXPORT_DAEMON} | grep -v "grep" | awk '{print $1}'`
    PID_STORAGED=`ps ax | grep ${LOCAL_STORAGE_DAEMON} | grep -v "grep" | awk '{print $1}'`

    if [ "$PID_STORAGED" != "" ] || [ "$PID_EXPORTD" != "" ]
    then
        echo "${LOCAL_EXPORT_DAEMON} or/and ${LOCAL_STORAGE_DAEMON} already running"
        exit 0;
    fi

}


do_stop()
{
        undeploy_clients_local
        stop_storaged
        stop_exportd
        remove_all


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
    echo >&2 "Usage:"
    echo >&2 "$0 start <Layout>"
    echo >&2 "$0 stop"
    echo >&2 "$0 reload"
    echo >&2 "$0 build"
    echo >&2 "$0 clean"
    echo >&2 "$0 pjd_test"
    echo >&2 "$0 fileop_test"
    echo >&2 "$0 mount"
    echo >&2 "$0 umount"
    exit 0;
}

main ()
{
    [ $# -lt 1 ] && usage


    # to reach storcli executable
    export PATH=$PATH:${LOCAL_BUILD_DIR}/src/storcli
    # to reach storcli_starter.sh  
    export PATH=$PATH:${LOCAL_SOURCE_DIR}/src/rozofsmount
    # to reach storio executable
    export PATH=$PATH:${LOCAL_BUILD_DIR}/src/storaged
    # to reach storio_starter.sh  
    export PATH=$PATH:${LOCAL_SOURCE_DIR}/src/storaged

    if [ "$1" == "start" ]
    then

        [ $# -lt 2 ] && usage

        if [ "$2" -eq 0 ]
        then
            ROZOFS_LAYOUT=$2
            STORAGES_BY_CLUSTER=4
        elif [ "$2" -eq 1 ]
        then
            ROZOFS_LAYOUT=$2
            STORAGES_BY_CLUSTER=8
        elif [ "$2" -eq 2 ]
        then
            ROZOFS_LAYOUT=$2
            STORAGES_BY_CLUSTER=16
        else
	        echo >&2 "Rozofs layout must be equal to 0,1 or 2."
	        exit 1
        fi

        check_build
        do_stop

        NB_EXPORTS=1
        NB_VOLUMES=1;
        NB_CLUSTERS_BY_VOLUME=1;

        gen_storage_conf ${STORAGES_BY_CLUSTER} 4
        gen_export_conf ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}

        go_layout ${ROZOFS_LAYOUT} ${STORAGES_BY_CLUSTER}

        create_storages
        create_exports

        start_storaged ${STORAGES_BY_CLUSTER}
        start_exportd

        deploy_clients_local

    elif [ "$1" == "stop" ]
    then
           do_stop

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
    elif [ "$1" == "clean" ]
    then
        clean_all
    else
        usage
    fi
    exit 0;
}

main $@
