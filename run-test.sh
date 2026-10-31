#!/bin/zsh
# Runs two copies of the game with the mod, from the isolated test folders, then stops only those copies.
#   ./run-test.sh <seconds> "<host [test] lines>" "<client [test] lines>"
set -u
cd "${0:A:h}/.."
seconds=$1
R="$HOME/Library/Application Support/Windows App Runner/Runtimes/GPTK 4.0 beta 3/wine"
stop() { for t in a b; do WINEPREFIX="$PWD/test-$t/prefix" "$R/bin/wineserver" -k 2>/dev/null; done }
trap stop EXIT
start() {
  ( cd "test-$1/game" && cp ../../mod/dist/winmm.dll . && rm -f fnaf4coop.log &&
    env WINEPREFIX="$PWD/../prefix" DYLD_FALLBACK_LIBRARY_PATH="$R/lib:/usr/lib" CX_ACTIVE_GRAPHICS_BACKEND=d3dmetal \
      WINEDEBUG=-all WINEMSYNC=1 ROSETTA_ADVERTISE_AVX=1 WINEDLLOVERRIDES='winmm=n,b' \
      nohup "$R/bin/wine" FiveNightsatFreddys4.exe > ../wine.log 2>&1 & )
}
printf '[coop]\nrole=host\nhost=\nport=47114\n[test]\ntest_mode=1\nskip_title_ms=3000\n%b\n' "$2" > test-a/game/fnaf4coop.ini
printf '[coop]\nrole=client\nhost=127.0.0.1\nport=47114\n[test]\ntest_mode=1\nskip_title_ms=3000\n%b\n' "$3" > test-b/game/fnaf4coop.ini
start a
perl -e 'select(undef,undef,undef,2)'
start b
end=$((SECONDS + seconds))
while [ $SECONDS -lt $end ]; do perl -e 'select(undef,undef,undef,1)'; done
for t in a b; do print "=== $t"; grep -vE 'object |frame has' "test-$t/game/fnaf4coop.log" | cut -c1-80; done
