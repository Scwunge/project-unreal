#!/bin/bash
# Before/after shot set for Unreal Mode across engine versions (5.8 -> 5.9).
# Usage: tools/unreal_ab.sh <tag>   (tag e.g. ue58 or ue59). Shots land in Saved/Remote/ab_<tag>_*.png (1070 only).
# Same scene, time, camera and delay for every run; Low and High; one log line per shot has the fps and frame ms.
tag=${1:?tag}
shots=(
 "unreal|-McScene=unreal -McTime=6000"
 "unreal_torch|-McScene=unreal -McTime=18000"
 "forest_sun|-McScene=forest_sun -McTime=500"
 "cave|-McScene=cave"
 "lake|-McScene=lake_low -McTime=6000"
 "shore_dark|-McScene=shore_dark -McTime=13000"
)
for q in 0 2; do
  for s in "${shots[@]}"; do
    n=${s%%|*}; a=${s#*|}
    MC_LOCAL_LANE=0 bash /e/mc-remote/remote.sh shot "ab_${tag}_${n}_q${q}.png" "-McUnreal -McQuality=$q $a -McShotDelay=12"
  done
done
