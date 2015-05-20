#!/bin/bash
set -e

users="$(grep -e "^u" sysusers.d/clear.conf | cut -d\   -f 2)"
groups="$(grep -e "^g" sysusers.d/clear.conf | cut -d\   -f 2)"
temp=$(mktemp -d)
mkdir $temp/etc

echo 1..$(echo $users $users $groups assigned | wc -w)

$SYSTEMDSYSUSERS --root $temp `pwd`/sysusers.d/clear.conf

i=1
for u in $users
do
   grep -q "^$u:" $temp/etc/passwd && r="ok" || r="not ok"
   echo "$r $i - user $u generated"
   ((i++))
done

for g in $users $groups
do
   grep -q "^$g:" $temp/etc/group && r="ok" || r="not ok"
   echo "$r $i - group $g generated"
   ((i++))
done

unassigned=$(grep -v -e "^#" -e "^$" -e "^[ug] [a-z0-9-]* [0-9]* .*" `pwd`/sysusers.d/clear.conf | grep -v '^m [a-z]* [a-z]*' || true)
if [ -z "$unassigned" ]
then
   r="ok"
else
   r="not ok"
fi
echo "$r $i - all ids are assigned"
echo $unassigned
