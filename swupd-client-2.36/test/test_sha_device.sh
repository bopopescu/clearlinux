#!/system/bin/sh
success=0
error=1
retValue=$success
ROOT_PATH=$(pwd)

# Checks for the root privilege
ID=$(id -u)
ID=${ID##*uid=}
# expect '0(root)'
ID=${ID:0:7}
if [ "$ID" != "0(root)" ]; then
	echo
	echo $0" should be run as root"
	exit $error
fi


#### mkdir -p $ROOT_PATH/sha
tar --preserve-permissions --selinux --xattrs-include='*' --xattrs -xf sha.tar
cd $ROOT_PATH/sha
for FILE in $(ls | grep -v '.sha')
do
	IFS=$'\n'
	SHA=($(eval "$ROOT_PATH/swupd_hashdump $FILE"))
	REF_SHA=$(cat $FILE.sha)
	if [ "${SHA[1]}" != "$REF_SHA" ]; then
		echo 'Bad SHA with file "'$FILE'"'
		echo "Computed SHA = "${SHA[1]}
		echo "Expected SHA = "$REF_SHA
		retValue=$error
	fi
done
cd - > /dev/null
rm -rf $ROOT_PATH/sha
if [ $retValue = $success ]; then
	echo "Test successful!"
else
	echo "Test failed!"
fi
exit $retValue
