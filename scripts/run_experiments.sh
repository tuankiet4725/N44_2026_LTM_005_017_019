#!/usr/bin/env bash
# Chạy toàn bộ thí nghiệm trên NS-3 và vẽ biểu đồ.
#   NS3_DIR=~/ns-3-dev RUNS=5 ./scripts/run_experiments.sh
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
NS3_DIR="${NS3_DIR:-$HOME/ns-3-dev}"
RUNS="${RUNS:-5}"
SIM_TIME="${SIM_TIME:-60}"
OUT="$PROJ/results/ns3_results.csv"

mkdir -p "$PROJ/results" "$PROJ/figures/ns3"
rm -f "$OUT"

# Sao chép mã nguồn vào scratch và build một lần
rm -rf "$NS3_DIR/scratch/emergency-qos"
cp -r "$PROJ/ns3/scratch/emergency-qos" "$NS3_DIR/scratch/"
cd "$NS3_DIR"
./ns3 build emergency-qos

run() { ./ns3 run --no-build "emergency-qos $* --simTime=$SIM_TIME --out=$OUT" > /dev/null; }

SCHEMES="fifo prio epq"

# Thí nghiệm A (RQ1, RQ3): quét tải đề nghị
for load in 0.6 0.8 1.0 1.2 1.4 1.6; do
  for s in $SCHEMES; do
    for r in $(seq 1 "$RUNS"); do
      echo "[A] scheme=$s load=$load run=$r"
      run --experiment=A --scheme=$s --load=$load --run=$r
    done
  done
done

# Thí nghiệm B (RQ2): "bão" dữ liệu khẩn cấp - kiểm tra hiện tượng đói băng thông
for es in 1 4 8 12 16 20; do
  for s in $SCHEMES; do
    for r in $(seq 1 "$RUNS"); do
      echo "[B] scheme=$s eScale=$es run=$r"
      run --experiment=B --scheme=$s --load=1.0 --eScale=$es --run=$r
    done
  done
done

# Thí nghiệm C (phân tích độ nhạy, tuỳ chọn): EPQ với các EmergencyRate khác nhau
for er in 300kbps 600kbps 1000kbps; do
  for r in $(seq 1 "$RUNS"); do
    echo "[C] eRate=$er run=$r"
    run --experiment=C_$er --scheme=epq --load=1.0 --eScale=12 --eRate=$er --run=$r
  done
done

python3 "$PROJ/scripts/plot_results.py" "$OUT" --outdir "$PROJ/figures/ns3" --label "NS-3"
echo "Xong. Biểu đồ: $PROJ/figures/ns3"
