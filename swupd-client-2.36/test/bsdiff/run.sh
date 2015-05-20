#!/bin/bash


echo -n "1.."
valgrind -q ../../swupd_bspatch data/1.bspatch.original 1.out data/1.bspatch.diff stdout
echo -n "2.."
valgrind -q ../../swupd_bspatch data/2.bspatch.original 2.out data/2.bspatch.diff stdout
echo -n "3.."
valgrind -q ../../swupd_bspatch data/3.bspatch.original 3.out data/3.bspatch.diff stdout
echo -n "4.."
valgrind -q ../../swupd_bspatch data/4.bspatch.original 4.out data/4.bspatch.diff stdout
echo -n "5.."
valgrind -q ../../swupd_bspatch data/5.bspatch.original 5.out data/5.bspatch.diff stdout
echo -n "6.."
valgrind -q ../../swupd_bspatch data/6.bspatch.original 6.out data/6.bspatch.diff stdout
echo -n "7.."
valgrind -q ../../swupd_bspatch data/7.bspatch.original 7.out data/7.bspatch.diff stdout
echo -n "8.."
valgrind -q ../../swupd_bspatch data/8.bspatch.original 8.out data/8.bspatch.diff stdout
echo -n "9.."
valgrind -q ../../swupd_bspatch data/9.bspatch.original 9.out data/9.bspatch.diff stdout
diff data/9.bspatch.modified 9.out
if [ $? -ne 0 ]
then
	echo "bspatch 9 output does not match expected!!"
fi
echo -n "10.."
valgrind -q ../../swupd_bspatch data/10.bspatch.original 10.out data/10.bspatch.diff stdout
diff data/10.bspatch.modified 10.out
if [ $? -ne 0 ]
then
	echo "bspatch 10 output does not match expected!!"
fi
#same as 9 but with zeros encoding
echo -n "11.."
valgrind -q ../../swupd_bspatch data/9.bspatch.original 11.out data/11.bspatch.diff stdout
diff data/9.bspatch.modified 11.out
if [ $? -ne 0 ]
then
	echo "bspatch 11 output does not match expected!!"
fi
echo -n "12.."
valgrind -q ../../swupd_bspatch data/12.bspatch.original 12.out data/12.bspatch.diff stdout
diff data/12.bspatch.modified 12.out
if [ $? -ne 0 ]
then
	echo "bspatch 12 output does not match expected!!"
fi
echo -n "13.."
valgrind -q ../../swupd_bsdiff data/13.bspatch.original data/13.bspatch.modified data/13.bspatch.diff any debug
valgrind -q ../../swupd_bspatch data/13.bspatch.original 13.out data/13.bspatch.diff stdout
diff data/13.bspatch.modified 13.out
if [ $? -ne 0 ]
then
	echo "bspatch 13 output does not match expected!!"
fi

# Next a very loooong running test, but one which successfully condenses the 2MB
# original file pair into a 26kB bsdiff.  The bsdiff computation alone (ie:
# non-valgrind'd) takes ~20minutes on a decent build machine.  Running it
# through valgrind takes many many hours to run to completion.  Therefore leave
# filepair #14 as one for only occasional use in long-running regression
# testing.  The other file pairs can be check quickly enough that they can be
# used in a regression test run at every check-in of code changes to the bsdiff
# implementation.
#
#echo -n "14.."
#valgrind -q ../../swupd_bsdiff data/14.bspatch.original data/14.bspatch.modified data/14.bspatch.diff any debug
#valgrind -q ../../swupd_bspatch data/14.bspatch.original 14.out data/14.bspatch.diff stdout
#diff data/14.bspatch.modified 14.out
#if [ $? -ne 0 ]
#then
#	echo "bspatch 14 output does not match expected!!"
#fi

echo -n "15.."
valgrind -q ../../swupd_bsdiff data/15.bspatch.original data/15.bspatch.modified data/15.bspatch.diff any debug
if [ $? -ne 255 ]
then
	echo "bspatch 15 creation has memory management issue!"
fi
