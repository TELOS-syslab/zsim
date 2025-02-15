config_name=test

if [ $# -eq 1 ];
then
		config_name=$1
fi
echo "config: ${config_name}"


if ! [ -f "/home/yxr/projects/learned_dram/zsim/tests/${config_name}.cfg" ];
then
	echo "Error!"
	echo "/home/yxr/projects/learned_dram/zsim/tests/${config_name}.cfg does not exists"
	exit 1
fi

mkdir -p ${config_name}

cd ${config_name}
echo "pwd: $(pwd)"
echo "../../build/opt/zsim ../../tests/${config_name}.cfg"

sleep 2
../../build/opt/zsim ../../tests/${config_name}.cfg
