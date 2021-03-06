#!/bin/bash

file=$1
singlePrecision=$2 # if this equals "s", then use single precision

scriptsdir="$HOME/DNA/scripts"

# Check if we have a .bz2 file, in that case: decompress first
fileBase=`basename $file`
fileBaseNoBzip=`basename $file .bz2`
if [ "$fileBaseNoBzip" != "$fileBase" ]
then
	# We have a bz2 file -> decompress first
	bzip2 -d $file
	file="`dirname $file`/$fileBaseNoBzip"
fi


if [ `file $file -b --mime-type` != 'text/plain' ]
then
	echo "The file '$file' is not a plain text file! Bailing out!"
	exit 1
fi

# Ok, this is really really REALLY ugly, but it looks like sometimes octave 
# gets stuck. So we spawn it off in the background, wait for a sufficiently 
# long time and if it still hasn't completed yet -> kill it and try again.
if [ "$singlePrecision" == "s" ]
then
	octave -q -p $scriptsdir --eval "toNative('$file', true)" &
	PID=$!
else
	octave -q -p $scriptsdir --eval "toNative('$file', false)" &
	PID=$!
fi

sleep 1s

while true # keep polling CPU usage
do
	cpuPerMil=`ps --cumulative S h o 'cp' p $PID`
	# I can't seem to get the total CPU time (including children [gzip]) in here ...
	if [ -z "$cpuPerMil" ]
	then
		echo "Exiting cleanly! :-)"
		# We couldn't find octave, so it stopped cleanly!
		exit 0
	fi

	#echo "Still running!"

	# Still running! Check if we are actually using CPU 
	# cycles, or if we are stuck
	#echo "cpuPerMil: $cpuPerMil"
	if [ $cpuPerMil -lt 2 ] # less than 0.2% cpu average usage (sufficiently low, because we can't detect child cpu usage!)
	then
		echo "Seems stuck! :-( KILLING!!!!!"
		# Seems stuck: KILL, KILL, KILL!
		kill $PID
		echo "Sent SIGTERM, waiting..."
		sleep 5s
		kill -9 $PID
		echo "Sent SIGKILL, waiting..."
		sleep 5s

		echo "Recursive call to try again!"
		# Call ourselves again, then exit cleanly
		$scriptsdir/toNative.sh "$file"
		#./$0 "$file"
		exit 0
	fi

	#echo "Still running: sleeping for a bit"
	#Sleep some more.
	sleep 0.8s
done

