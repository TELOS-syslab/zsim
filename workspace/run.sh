config_name=test.cfg

if [ $# -eq 1 ];
then
		config_name=$1
fi

echo "../build/opt/zsim ../tests/${config_name}"

sleep 2
../build/opt/zsim ../tests/${config_name}
