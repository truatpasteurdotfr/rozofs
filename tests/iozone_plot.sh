#
# This script will create the Iozone graphs using
# gnuplot. 
#
#
#
# ------------------------------------------------
# YOU MUST PROVIDE A FILE NAME FOR IT TO PROCESS.
# This filename is the name of the file where you 
# sent Iozone's output.
# ------------------------------------------------

# Generate data base for all of the operation types.

. env.sh

usage() {
	echo "$0: <log file>"
	exit 0
}

[[ $# -lt 1 ]] && usage

[[ -z ${GNUPLOT_BINARY} ]] && echo "Can't find gnuplot." && exit -1

./gengnuplot.sh $1 write
./gengnuplot.sh $1 rewrite
./gengnuplot.sh $1 read
./gengnuplot.sh $1 reread
./gengnuplot.sh $1 randread
./gengnuplot.sh $1 randwrite
./gengnuplot.sh $1 bkwdread
./gengnuplot.sh $1 recrewrite
./gengnuplot.sh $1 strideread
./gengnuplot.sh $1 fwrite
./gengnuplot.sh $1 frewrite
./gengnuplot.sh $1 fread
./gengnuplot.sh $1 freread

# Produce graphs and postscript results.
${GNUPLOT_BINARY} gnu3d.dem
