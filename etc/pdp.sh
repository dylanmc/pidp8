procs=`screen -ls pidp | egrep '[0-9]+\.pidp' | wc -l`
echo $procs
if [ $procs -ne 0 ]; then
	screen -r
fi
