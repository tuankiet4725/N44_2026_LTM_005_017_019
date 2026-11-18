#!/usr/bin/env python3
"""
Mô hình tham chiếu (Python, sự kiện rời rạc) của nút cổ chai trong kịch bản NS-3.

Mục đích:
  * Kiểm chứng logic thuật toán EPQ độc lập với NS-3 (cùng tham số, cùng lưu lượng).
  * Sinh biểu đồ sơ bộ khi chưa chạy NS-3.
Khác biệt so với NS-3: luồng TCP bulk được thay bằng nguồn Poisson 300 kb/s,
không mô hình hoá header lớp 2 và động học TCP. Kết quả cuối cùng của báo cáo
phải lấy từ NS-3 (scripts/run_experiments.sh).

Xuất CSV cùng định dạng với NS-3 nên dùng chung plot_results.py.
"""
import argparse
import csv
import os
import random
from collections import deque

E, C, BE = 0, 1, 2
NAMES = ["emergency", "critical", "best_effort"]
HDR = 28           # IP + UDP
CAP = 2_000_000    # b/s, nút cổ chai
BASE_DELAY = 0.0145  # truyền dẫn + lan truyền trên các liên kết truy nhập (s)


# --------------------------- Queue disciplines ---------------------------
class Fifo:
    def __init__(self, limit=100):
        self.q, self.limit, self.pushouts = deque(), limit, 0

    def enqueue(self, p, now, drop):
        if len(self.q) >= self.limit:
            drop(p); return
        self.q.append(p)

    def dequeue(self, now):
        return self.q.popleft() if self.q else None


class Prio:
    def __init__(self, limit=100):
        e = limit * 3 // 10
        c = (limit - e) // 2
        self.lim = [e, c, limit - e - c]
        self.q = [deque(), deque(), deque()]
        self.pushouts = 0

    def enqueue(self, p, now, drop):
        q = self.q[p[0]]
        if len(q) >= self.lim[p[0]]:
            drop(p); return
        q.append(p)

    def dequeue(self, now):
        for q in self.q:
            if q:
                return q.popleft()
        return None


class EPQ:
    """Bản sao 1-1 của ns3::EmergencyQueueDisc."""
    def __init__(self, limit=100, e_rate=600_000, burst=15000, push_out=True,
                 quanta=(1500, 4500, 1500)):
        self.limit, self.e_lim = limit, limit * 3 // 10
        self.rate, self.burst, self.push_out = e_rate / 8.0, burst, push_out
        self.quanta = quanta
        self.q = [deque(), deque(), deque()]
        self.tokens, self.last = float(burst), 0.0
        self.deficit = [0, 0, 0]
        self.rr, self.granted = E, False
        self.pushouts = self.strict = self.demoted = 0

    def size(self):
        return len(self.q[0]) + len(self.q[1]) + len(self.q[2])

    def enqueue(self, p, now, drop):
        c = p[0]
        if c == E and len(self.q[E]) >= self.e_lim:
            drop(p); return
        if self.size() >= self.limit:
            if self.push_out and c != BE:
                for v in range(BE, c, -1):
                    while self.size() >= self.limit and self.q[v]:
                        drop(self.q[v].popleft())
                        self.pushouts += 1
            if self.size() >= self.limit:
                drop(p); return
        self.q[c].append(p)

    def _next(self):
        self.rr = (self.rr + 1) % 3
        self.granted = False

    def dequeue(self, now):
        self.tokens = min(self.burst, self.tokens + (now - self.last) * self.rate)
        self.last = now
        eq = self.q[E]
        if eq and self.tokens >= eq[0][1]:
            self.tokens -= eq[0][1]
            self.strict += 1
            return eq.popleft()
        if not self.size():
            return None
        while True:
            q = self.q[self.rr]
            if not q:
                self.deficit[self.rr] = 0; self._next(); continue
            if not self.granted:
                self.deficit[self.rr] += self.quanta[self.rr]; self.granted = True
            sz = q[0][1]
            if self.deficit[self.rr] >= sz:
                self.deficit[self.rr] -= sz
                p = q.popleft()
                if self.rr == E:
                    self.demoted += 1
                if not q:
                    self.deficit[self.rr] = 0; self._next()
                return p
            self._next()


# --------------------------- Traffic sources ---------------------------
def cbr(rng, cls, flow, rate, payload, t0, t1):
    out, iv = [], payload * 8.0 / rate
    t = t0 + rng.uniform(0, iv)
    while t < t1:
        out.append((t, cls, flow, payload + HDR)); t += iv
    return out


def onoff(rng, cls, flow, peak, payload, on_m, off_m, t0, t1):
    out, iv, t = [], payload * 8.0 / peak, t0
    while t < t1:
        on = rng.expovariate(1 / on_m)
        k = t
        while k < min(t + on, t1):
            out.append((k, cls, flow, payload + HDR)); k += iv
        t += on + rng.expovariate(1 / off_m)
    return out


def poisson(rng, cls, flow, rate, payload, t0, t1):
    out, lam, t = [], rate / (payload * 8.0), t0
    while True:
        t += rng.expovariate(lam)
        if t >= t1:
            return out
        out.append((t, cls, flow, payload + HDR))


def make_traffic(rng, load, e_scale, n_e, sim_time):
    t0 = 1.0
    e_peak = 64000 * e_scale
    e_avg = n_e * e_peak * 0.25
    be_rate = max(50_000, load * CAP - e_avg - 2 * 500_000)
    pk = []
    for i in range(n_e):
        pk += onoff(rng, E, f"e{i}", e_peak, 200, 0.5, 1.5, t0 + 0.013 * i, sim_time)
    for i in range(2):
        pk += cbr(rng, C, f"c{i}", 500_000, 1000, t0, sim_time)
    pk += cbr(rng, BE, "b0", be_rate, 1000, t0, sim_time)
    pk += poisson(rng, BE, "b1", 300_000, 1000, t0, sim_time)  # thay cho TCP bulk
    pk.sort(key=lambda x: x[0])
    return pk


# --------------------------- Simulation core ---------------------------
def simulate(scheme, load, e_scale, n_e, run, sim_time, e_rate=600_000):
    rng = random.Random(1000 * run + 7)
    arrivals = make_traffic(rng, load, e_scale, n_e, sim_time)
    qd = {"fifo": Fifo, "prio": Prio}.get(scheme, None)
    qd = qd() if qd else EPQ(e_rate=e_rate)

    tx = [0, 0, 0]; rx = [0, 0, 0]; rxb = [0, 0, 0]
    delays = [[], [], []]
    last_d = {}; jit = [0.0, 0.0, 0.0]; jit_n = [0, 0, 0]

    def drop(p):
        pass  # loss = tx - rx

    i, n = 0, len(arrivals)
    busy, t_dep, cur = False, float("inf"), None
    while i < n or busy:
        if i < n and arrivals[i][0] <= t_dep:
            t, cls, flow, size = arrivals[i]; i += 1
            tx[cls] += 1
            qd.enqueue((cls, size, flow, t), t, drop)
            if not busy:
                cur = qd.dequeue(t)
                if cur:
                    busy, t_dep = True, t + cur[1] * 8.0 / CAP
        else:
            t = t_dep
            cls, size, flow, ta = cur
            d = t - ta + BASE_DELAY
            rx[cls] += 1; rxb[cls] += size - HDR
            delays[cls].append(d)
            if flow in last_d:
                jit[cls] += abs(d - last_d[flow]); jit_n[cls] += 1
            last_d[flow] = d
            cur = qd.dequeue(t)
            if cur:
                t_dep = t + cur[1] * 8.0 / CAP
            else:
                busy, t_dep = False, float("inf")

    rows = []
    for k in range(3):
        ds = sorted(delays[k])
        p95 = ds[int(0.95 * (len(ds) - 1))] if ds else 0
        rows.append(dict(
            scheme=scheme, load=load, e_scale=e_scale, n_emergency=n_e, run=run,
            **{"class": NAMES[k]}, tx_pkts=tx[k], rx_pkts=rx[k],
            loss_pct=100.0 * (tx[k] - rx[k]) / tx[k] if tx[k] else 0,
            throughput_kbps=rxb[k] * 8 / (sim_time - 1.0) / 1000,
            mean_delay_ms=1000 * sum(ds) / len(ds) if ds else 0,
            p95_delay_ms=1000 * p95,
            jitter_ms=1000 * jit[k] / jit_n[k] if jit_n[k] else 0,
            pushouts=qd.pushouts))
    return rows


FIELDS = ["experiment", "scheme", "load", "e_scale", "n_emergency", "run", "class", "tx_pkts",
          "rx_pkts", "loss_pct", "throughput_kbps", "mean_delay_ms", "p95_delay_ms",
          "jitter_ms", "pushouts"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="results/reference_results.csv")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--sim-time", type=float, default=60.0)
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS); w.writeheader()
        for load in (0.6, 0.8, 1.0, 1.2, 1.4, 1.6):
            for s in ("fifo", "prio", "epq"):
                for r in range(1, a.runs + 1):
                    for row in simulate(s, load, 1.0, 10, r, a.sim_time):
                        w.writerow({"experiment": "A", **row})
            print(f"[A] load={load} done", flush=True)
        for es in (1, 4, 8, 12, 16, 20):
            for s in ("fifo", "prio", "epq"):
                for r in range(1, a.runs + 1):
                    for row in simulate(s, 1.0, es, 10, r, a.sim_time):
                        w.writerow({"experiment": "B", **row})
            print(f"[B] eScale={es} done", flush=True)
        for er in (300_000, 600_000, 1_000_000):
            for r in range(1, a.runs + 1):
                for row in simulate("epq", 1.0, 12, 10, r, a.sim_time, e_rate=er):
                    w.writerow({"experiment": f"C_{er // 1000}kbps", **row})
        print("[C] done")


if __name__ == "__main__":
    main()
