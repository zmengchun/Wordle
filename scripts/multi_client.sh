#!/bin/bash
# 用法: ./scripts/multi_client.sh [client數量] [停留秒數] [client執行檔]
N=${1:-12}
HOLD=${2:-15}
BIN=${3:-./build/client}
PORT=9000

rm -f /tmp/client_*.log

for i in $(seq 1 "$N"); do
  (
    echo START;          sleep 1
    echo "GUESS crane";  sleep 1
    echo "GUESS house";  sleep "$HOLD"
    echo QUIT
  ) | "$BIN" > /tmp/client_$i.log 2>&1 &
done
echo ">>> launched $N clients, each holds the connection ~$((HOLD+2))s"

sleep 5   # 等所有 client 都連上、都在遊戲中

P=$(pgrep -o -x server)
echo
echo "===== CONCURRENCY PROOF ($(date +%T)) ====="
echo "server parent pid : $P"
echo "child processes   : $(pgrep -P "$P" | wc -l)"
echo "ESTABLISHED conns : $(ss -tn state established "( sport = :$PORT )" | tail -n +2 | wc -l)"
echo
pstree -p "$P"
echo "==========================================="

wait
echo
echo ">>> all clients finished"
echo "clients that got a RESULT reply: $(grep -l RESULT /tmp/client_*.log | wc -l) / $N"
echo "zombies left: $(ps -eo stat,cmd | grep -c '^Z.*server')"