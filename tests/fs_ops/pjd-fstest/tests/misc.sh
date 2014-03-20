# $FreeBSD: src/tools/regression/fstest/tests/misc.sh,v 1.1 2007/01/17 01:42:08 pjd Exp $

ntest=1

name253="_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_12"
name255="${name253}34"
name256="${name255}5"
path1021="${name255}/${name255}/${name255}/${name253}"
path1023="${path1021}/x"
path1024="${path1023}x"

echo ${dir} | egrep '^/' >/dev/null 2>&1
if [ $? -eq 0 ]; then
	maindir="${dir}/../.."
else
	maindir="`pwd`/${dir}/../.."
fi
fstest="${maindir}/fstest"
. ${maindir}/tests/conf


# Execution trace
subject=`echo ${dir} | awk -F'/' '{print $NF }'`
test=`basename $0`
case "${PJD_TRACE}" in
 "") TRACE_DIR=/tmp/pjd_result/$subject;;
 *)  TRACE_DIR=${PJD_TRACE}/$subject;;
esac 
mkdir -p $TRACE_DIR
TRACE_FILE="$TRACE_DIR/$test.res"
zedate=`date`
printf "\n%s/%s[  0] : %s\n" ${subject} ${test} "${zedate}" > $TRACE_FILE



run_getconf()
{
	if val=$(getconf "${1}" .); then
		if [ "$value" = "undefined" ]; then
			echo "${1} is undefined"
			exit 1
		fi
	else
		echo "Failed to get ${1}"
		exit 1
	fi

	echo $val
}

name_max_val=$(run_getconf NAME_MAX)
path_max_val=$(run_getconf PATH_MAX)

name_max="_"
i=1
while test $i -lt $name_max_val ; do 
	name_max="${name_max}x"
	i=$(($i+1))
done

num_of_dirs=$(( ($path_max_val + $name_max_val) / ($name_max_val + 1) - 1 ))

long_dir="${name_max}"
i=1
while test $i -lt $num_of_dirs ; do 
	long_dir="${long_dir}/${name_max}"
	i=$(($i+1))
done
long_dir="${long_dir}/x"

too_long="${long_dir}/${name_max}"


test_is_failed() {
  printf "\n!!!\nTest is failed\n!!!\n "
} >> $TRACE_FILE

test_begin() {
  printf "%s/%s[%3d] : $*\n" ${subject} ${test} ${ntest}
} >> $TRACE_FILE

create_too_long()
{
	mkdir -p ${long_dir}
}

unlink_too_long()
{
	rm -rf ${name_max}
}

expect()
{
        test_begin "expect $*"  
	e="${1}"
	shift
	r=`${fstest} $* 2>/dev/null | tail -1`
	echo "${r}" | egrep '^'${e}'$' >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo "ok ${ntest}"
	else
		echo "not ok ${ntest}"
		test_is_failed
	fi
	ntest=`expr $ntest + 1`
}

jexpect()
{
        test_begin "jexpect $*" 
	s="${1}"
	d="${2}"
	e="${3}"
	shift 3
	r=`jail -s ${s} / fstest 127.0.0.1 /bin/sh -c "cd ${d} && ${fstest} $* 2>/dev/null" | tail -1`
	echo "${r}" | egrep '^'${e}'$' >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo "ok ${ntest}"
	else
		echo "not ok ${ntest}"
		test_is_failed

	fi
	ntest=`expr $ntest + 1`
}

test_check()
{
        test_begin "test_check $*" 

	if [ $* ]; then
		echo "ok ${ntest}"
	else
		echo "not ok ${ntest}"
		test_is_failed


	fi
	ntest=`expr $ntest + 1`
}

namegen()
{
	echo "fstest_`dd if=/dev/urandom bs=1k count=1 2>/dev/null | md5sum  | cut -f1 -d' '`"
}

quick_exit()
{
	echo "1..1"
	echo "ok 1"
	exit 0
}

supported()
{
	case "${1}" in
	chflags)
		if [ ${os} != "FreeBSD" -o ${fs} != "UFS" ]; then
			return 1
		fi
		;;
	lchmod)
		if [ ${os} != "FreeBSD" ]; then
			return 1
		fi
		;;
	esac
	return 0
}

require()
{
	if supported ${1}; then
		return
	fi
	quick_exit
}
