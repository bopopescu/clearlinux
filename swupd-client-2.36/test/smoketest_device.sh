#!/bin/bash

echo "This is an android specific test which needs porting."
exit -1


# 'ANDROID_BUILD_TOP' is defined after sourced build/envsetup.sh and
# lunch <TARGET>
SP_UPDATE_CLIENT="$ANDROID_BUILD_TOP/sp-private/sp-update-client"
SYSTEM_BIN_PATH="$ANDROID_BUILD_TOP/out/target/product/starpeak/system/bin/"
# Smoke Test Path on device
SWUPDP="/data/swupd"
DEVP="$SWUPDP/UT"
SUNAME="su"
SU_PATH="/system/xbin/$SUNAME"
BTRFS_MOUNTPOINT="/mnt/swupd"
ESP_MOUNTPOINT=$BTRFS_MOUNTPOINT"/update/esp"

# For Debug traces, DBG_OUT="/dev/fd/1" on Android
DBG_OUT="/dev/null"
#DBG_OUT="/dev/fd/2"
ERR_OUT="/dev/stderr"

#define some constant
success=0
error=1
true=1
false=0
B_TO_KB=" / 1024"
B_TO_MB=$B_TO_KB" / 1024"
MB_TO_B="* 1024 * 1024"

#define some local aliases
shopt -s expand_aliases
alias exit_on_error='err=$?; if [ "$err" != "$success" ]; then echo -e "\nTEST FAILED. LOG=$LOG"; remove_folder_tree; exit $err; fi'
alias exit_on_success='err=$?; if [ "$err" == "$success" ]; then echo -e "\nTEST FAILED. LOG=$LOG"; remove_folder_tree; exit $error; fi'


IP=$1
is_connected() {
	CHECK=$(adb shell id 2>&1)
	echo $CHECK
	if [ "${CHECK:0:7}" == "error: " ]; then
		return $error
	else
		return $success
	fi
}

connect() {
	connected=$false
	CHECK=$(is_connected)
	if [ "$?" != "$success" ]; then
		if [ "$IP" != "" ]; then
			echo "Connecting... : adb connect $IP"
			attempt=3
			while [ $attempt -gt 0 ]
			do
				adb connect $IP 1>/dev/null
				exit_on_error
				CHECK=$(is_connected)
				if [ "$?" != "$success" ]; then
					attempt=$(($attempt - 1))
					echo $CHECK > $ERR_OUT
					echo "Attempt=$attempt" > $DBG_OUT
				else
					attempt=0
					connected=$true
				fi
			done
		else
			echo $CHECK > $ERR_OUT
			echo "You could define IP address of your device as first parameter of this script" > $ERR_OUT
		fi
	else
		connected=$true
	fi
	if [ "$connected" != "$true" ]; then
		LOG="Unable to connect to device at IP=$IP"
		return $error
	fi
}

adb_push() {
	HOST_FILE=$1
	if [ ! -e "$HOST_FILE" ]; then
		echo "$HOST_FILE doesn't exist"
		remove_folder_tree
		exit $error
	else
		echo "Pushing $HOST_FILE"
	fi
	adb push "$HOST_FILE" $DEVP > $DBG_OUT
}

convert_error() {
	local ERR=$(echo $1 | sed "s/'//g")
	if [ "$ERR" != "0" ]; then
		ERR=1
	fi
	return $ERR
}

adb_shell() {
	CMD=$@" 2>&1"
	# add command as a workaround (because adb shell doesn't return code of executed command
	CMD_WA="$CMD; err=\$?; if [ \"\$err\" != \"0\" ]; then echo \"ERROR = \$err\"; fi"
	echo "[ $CMD ]" > $DBG_OUT
	LOG=$(adb shell "$CMD_WA")
	IS_ERROR=$(echo $LOG  | grep "ERROR =")
	ERR_CODE=${LOG##*ERROR =}
	if [ "$LOG" != "" ]; then
		echo "$LOG"
	fi
	if [ "$IS_ERROR" != "" ]; then
		echo "adb shell failed with command [ $CMD ]" > $DBG_OUT
		convert_error $ERR_CODE
		return $?
	fi

}

mount_btrfs_as_rw () {
	BTRFS_MOUNTED=$(adb_shell "mount" | grep $BTRFS_MOUNTPOINT)
	if [ "$BTRFS_MOUNTED" == "" ]; then
		echo "Mount BTRFS as RW"
		adb_shell "$SU_PATH -c mount -t btrfs /dev/block/by-uuid/632b73c9-3b78-4ee5-a856-067bb4e1b745 $BTRFS_MOUNTPOINT"
		exit_on_error
	fi
}

mount_esp_as_rw () {
	ESP_MOUNTED=$(adb_shell "mount" | grep $ESP_MOUNTPOINT)
	if [ "$ESP_MOUNTED" == "" ]; then
		mount_btrfs_as_rw
		echo "Mount ESP as RW"
		adb_shell "$SU_PATH -c mkdir -p $ESP_MOUNTPOINT"
		exit_on_error
		adb_shell "$SU_PATH -c mount -t vfat /dev/block/by-uuid/c12a7328-f81f-11d2-ba4b-00a0c93ec93b $ESP_MOUNTPOINT"
		exit_on_error
	fi
}


install_su_as_root () {
	# switch to root
	switch_as_root
	# Push 'su' binary
	adb push "$ANDROID_PRODUCT_OUT$SU_PATH" "$SWUPDP"
	SU_PATH="$SWUPDP/$SUNAME"
	adb_shell 'attr -s security.selinux -V u:object_r:su_exec:s0 '$SU_PATH
	switch_as_user
}

check_su() {
	local ERR
	# First of all, be sure 'su' binary is present on device
	adb_shell "test -e $SU_PATH"
	if [ "$?" != "$success" ]; then
		# check 'su' binary is present on host (build tree)
		if [ ! -e "$ANDROID_PRODUCT_OUT$SU_PATH" ]; then
			echo "unable to find '$SU_PATH' binary. Test Failed" > $ERR_OUT
			exit $error
		else
			install_su_as_root
		fi
	fi
}

get_diskspace() {
	PART=$1
	FREE=$(adb_shell "$SU_PATH -c $DEVP/swupd_get_diskspace "$PART)
	# Remove newline character
	LEN=${#FREE}
	FREE=${FREE:0:$(($LEN - 1))}
	# return result
	echo $FREE
}


install_tests() {
	pushd $SYSTEM_BIN_PATH 1>/dev/null
		echo "Uploading tests..."
		adb_push swupd_hashtest
		adb_push swupd_tmfmttest
		adb_push swupd_listtest
		adb_push swupd_locktest
		adb_push swupd_executortest
		adb_push swupd_sig_verifytest
		adb_push swupd_get_diskspace
		adb_push swupd_hashdump
		pushd $SP_UPDATE_CLIENT/test/signature 1>/dev/null
			./sign.sh
			adb_push my-data
			adb_push my-data.signed
			adb_push ca.cert.pem
			adb_push ca.cert.test.bad.pem
			# keep a copy
			adb_shell "cp  $DEVP/my-data $DEVP/my-data.ok"
			exit_on_error
			adb_shell "cp  $DEVP/my-data.signed $DEVP/my-data.signed.ok"
			exit_on_error
		popd 1>/dev/null
		pushd $SP_UPDATE_CLIENT/test 1>/dev/null
			adb_push test_sha_device.sh $DEVP
			adb_push sha.tar $DEVP
		popd 1>/dev/null
	popd 1>/dev/null
}

create_test_folder_tree() {
	echo "Create smoke test folder tree on device"
	adb_shell "$SU_PATH -c mkdir -p $SWUPDP/"; exit_on_error
	adb_shell "$SU_PATH -c chmod -R 777 $SWUPDP"; exit_on_error
	adb_shell "$SU_PATH -c mkdir -p $DEVP/"; exit_on_error
	adb_shell "$SU_PATH -c chmod -R 777 $DEVP"; exit_on_error
	adb_shell "$SU_PATH -c mkdir -p $DEVP/ro"; exit_on_error
	adb_shell "$SU_PATH -c chmod 444 $DEVP/ro"; exit_on_error
	adb_shell "$SU_PATH -c mkdir -p $DEVP/rw"; exit_on_error
	adb_shell "$SU_PATH -c chmod 777 $DEVP/rw"; exit_on_error
}

remove_folder_tree() {
	# Used to try to understand btrfs space management...
	if [ "1" != "0" ]; then

	if [ "$connected" == "$true" ]; then
		echo "Remove smoke test folder tree on device"
		adb shell "$SU_PATH -c rm -rf $DEVP"
		echo "Remove fake files created to fill disk space"
		adb shell "$SU_PATH -c rm $BTRFS_MOUNTPOINT/fake"
		adb shell "$SU_PATH -c rm $ESP_MOUNTPOINT/fake"
		echo "Umount ESP mount point"
		adb shell "$SU_PATH -c umount $ESP_MOUNTPOINT"
		echo "Umount BTRFS mount point"
		adb shell "$SU_PATH -c umount $BTRFS_MOUNTPOINT"
	fi

	fi
}

main() {
	adb kill-server
	connect
	exit_on_error
	check_su
	create_test_folder_tree
	install_tests

	# Check Telemetry formating
	echo -ne "TEST Telemetry (swupd_tmfmttest): "
	LOG=$(adb_shell "$SU_PATH -c $DEVP/swupd_tmfmttest")
	CHECK=$(echo $LOG | grep OK)
	exit_on_error
	echo "OK"

	# Check hashcode computing
	echo -ne "TEST SHA computing : "
	LOG=$(adb_shell "cd $DEVP; $SU_PATH -c sh -e $DEVP/test_sha_device.sh")
	PATTERN="Test successful!"
	CHECK=$(echo $LOG | grep "$PATTERN")
	# PATTERN must be found
	exit_on_error
	echo "OK"

	# Check extended attributes support
	# swupd_hashtest : positive test
	echo -ne "TEST extended attributes (swupd_hashtest): "
	LOG=$(adb_shell $DEVP/swupd_hashtest xattrs test $DEVP/rw)
	PATTERN="FAILED"
	CHECK=$(echo $LOG | grep "$PATTERN")
	# PATTERN must not be found
	exit_on_success
	echo "OK"

	# swupd_hashtest : negative test
	LOG=$(adb_shell $DEVP/swupd_hashtest xattrs test $DEVP/ro)
	PATTERN="FAILED"
	CHECK=$(echo $LOG | grep "$PATTERN")
	# PATTERN must be found
	exit_on_error

	# Check Linked list
	echo -ne "TEST linked list (swupd_listtest): "
	LOG=$(adb_shell $DEVP/swupd_listtest)
	PATTERN="FAILED"
	CHECK=$(echo $LOG | grep "$PATTERN")
	# PATTERN must not be found
	exit_on_success
	echo "OK"

	# Check Lock file management
	echo -ne "TEST lock file (swupd_locktest): "
	LOG=$(adb_shell $DEVP/swupd_locktest)
	exit_on_error
	echo "OK"

	# Check multi threading management
	echo -ne "TEST multi-threads (swupd_executortest): "
	LOG=$(adb_shell $DEVP/swupd_executortest)
	PATTERN="(CORRECT)"
	CHECK=$(echo $LOG | grep "$PATTERN")
	# PATTERN must be found
	exit_on_error
	echo "OK"

	# Check diskspace API
	BLOCK_SIZE=1024

	adb_shell "$SU_PATH -c mount -o remount,rw / /"
	exit_on_error
	echo -ne "TEST diskspace API on ESP partition : "
	mount_esp_as_rw
	exit_on_error
	TEST=$(adb_shell "$SU_PATH -c ls -l $ESP_MOUNTPOINT" | grep fake)
	if [ "$TEST" != "" ]; then
		adb_shell "$SU_PATH -c rm $ESP_MOUNTPOINT/fake"
	fi
	adb_shell "sync"
	ESP_SPACE=$(get_diskspace 'esp')
	echo "Esp Free Disk space = "$ESP_SPACE" bytes"
	echo "Create a fake file of size: "$(( $ESP_SPACE $B_TO_MB ))" MB"
	# convert in number of blocks
	FAKE_FILE_SIZE=$(($ESP_SPACE / $BLOCK_SIZE))

	# fill the partition with a fake file:
	adb_shell $SU_PATH "-c dd if=/dev/zero of=$ESP_MOUNTPOINT/fake  bs=$BLOCK_SIZE count=$FAKE_FILE_SIZE"
	if [ "$?" != "$success" ]; then
		echo -e "dd command failed : \n $LOG" > $ERR_OUT
	fi
	adb_shell "sync"
	exit_on_error

	TOLERANCE=$BLOCK_SIZE
	ESP_SPACE=$(get_diskspace 'esp')
	echo "Remaining size $ESP_SPACE bytes $(( $ESP_SPACE $B_TO_MB )) MB (expected <= $TOLERANCE bytes $(($TOLERANCE $B_TO_MB)) MB)"
	if [ "$ESP_SPACE" -ge "$TOLERANCE" ]; then
		echo "Test diskspace API on ESP partition failed"
		exit $error
	fi
	echo "OK"



	echo -ne "TEST diskspace API on BTRFS partition : "
	echo "Mount BTRFS partition as RW..." > $DBG_OUT
	mount_btrfs_as_rw
	exit_on_error
	TEST=$(adb_shell "$SU_PATH -c ls -l $BTRFS_MOUNTPOINT" | grep 'fake')
	if [ "$TEST" != "" ]; then
		adb_shell "$SU_PATH -c rm $BTRFS_MOUNTPOINT/fake"
	fi
	adb_shell "sync"
	adb_shell "btrfs filesystem sync $BTRFS_MOUNTPOINT"
	BTRFS_SPACE=$(get_diskspace 'btrfs')
	echo "Btrfs Free Disk space = "$BTRFS_SPACE" bytes = "$(( $BTRFS_SPACE $B_TO_MB ))" MB"
	MARGIN=10
	echo "MARGIN=$MARGIN %"
	# Compute Margin in MB
	MARGIN=$(( $BTRFS_SPACE * $MARGIN / 100))
	echo "MARGIN SIZE=$(( $MARGIN $B_TO_MB )) MB"
	FAKE_FILE_SIZE=$BTRFS_SPACE
	FAKE_FILE_SIZE=$(( $FAKE_FILE_SIZE - $MARGIN))
	echo "Create a fake file of size: "$(( $FAKE_FILE_SIZE $B_TO_MB ))" MB"
	# convert in number of blocks
	FAKE_FILE_SIZE=$(($FAKE_FILE_SIZE / $BLOCK_SIZE))

	# fill the partition with a fake file:
	adb_shell "$SU_PATH -c dd if=/dev/zero of=$BTRFS_MOUNTPOINT/fake  bs=$BLOCK_SIZE count=$FAKE_FILE_SIZE"
	if [ "$?" != "$success" ]; then
		echo -e "dd command failed : \n $LOG" > $ERR_OUT
	fi
	adb_shell "sync"
	exit_on_error
	adb_shell "btrfs filesystem sync $BTRFS_MOUNTPOINT"
	exit_on_error

	TOLERANCE=$MARGIN
	BTRFS_SPACE=$(get_diskspace 'btrfs')
	echo "Remaining size $BTRFS_SPACE bytes $(( $BTRFS_SPACE $B_TO_MB )) MB (expected <= $TOLERANCE bytes $(($TOLERANCE $B_TO_MB)) MB)"
	if [ "$BTRFS_SPACE" -ge $TOLERANCE ]; then
		echo "Test diskspace API on BTRFS partition failed"
		exit $error
	fi
	echo "OK"


	# Check signature
	echo -ne "TEST signature (swupd_sig_verifytest) positive test: "
	LOG=$(adb_shell "cd $DEVP; ./swupd_sig_verifytest my-data my-data.signed ca.cert.pem")
	PATTERN="successful!"
	CHECK=$(echo $LOG | grep "$PATTERN")
	# PATTERN must be found
	exit_on_error
	echo "OK"

	# All these negative tests expects real implementation. Disable for now
	if [ "1" = "0" ]; then
		# swupd_sig_verifytest : negative test
		echo -ne "TEST signature (swupd_sig_verifytest) negative test, modify only data: "
		adb_shell 'echo "XX" >> '$DEVP'/my-data'
		LOG=$(adb_shell "cd $DEVP; ./swupd_sig_verifytest my-data my-data.signed ca.cert.pem")
		PATTERN="Verification failed!"
		CHECK=$(echo $LOG | grep "$PATTERN")
		# PATTERN must be found
		exit_on_error
		echo "OK"
		# Restore right my_data
		adb_shell "cp  $DEVP/my-data.ok $DEVP/my-data"

		# swupd_sig_verifytest : negative test
		echo -ne "TEST signature (swupd_sig_verifytest) negative test, used bad signed data: "
		adb_shell 'echo "XX" >> '$DEVP'/my-data.signed'
		LOG=$(adb_shell "cd $DEVP; ./swupd_sig_verifytest my-data my-data ca.cert.pem")
		PATTERN="Verification failed!"
		CHECK=$(echo $LOG | grep "$PATTERN")
		# PATTERN must be found
		exit_on_error
		echo "OK"
		# Restore right my_data.signed
		adb_shell "cp  $DEVP/my-data.signed.ok $DEVP/my-data.signed"

		# swupd_sig_verifytest : negative test
		echo -ne "TEST signature (swupd_sig_verifytest) negative test, used bad certificate: "
		adb_shell 'cd '$DEVP'; CERT=$(cat ca.cert.pem); BAD_CERT=${CERT/FfTCCA2Wg/BADBADBAD}; echo "$BAD_CERT" > bad.cert.pem'
		LOG=$(adb_shell "cd $DEVP; ./swupd_sig_verifytest my-data my-data.signed ca.cert.test.bad.pem")
		PATTERN="Verification failed!"
		CHECK=$(echo $LOG | grep "$PATTERN")
		# PATTERN must be found
		exit_on_error
		echo "OK"
	fi

	remove_folder_tree
}

main
