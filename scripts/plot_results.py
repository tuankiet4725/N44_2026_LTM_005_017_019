#!/usr/bin/env python3
"""Vẽ biểu đồ TRƯỚC/SAU và bảng tóm tắt từ CSV (NS-3 hoặc mô hình tham chiếu).

    python3 scripts/plot_results.py results/ns3_results.csv --outdir figures/ns3 --label NS-3
"""
import argparse
import csv
import math
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SCHEMES = ["fifo", "prio", "epq"]
LABEL = {"fifo": "FIFO (trước)", "prio": "Strict Priority", "epq": "EPQ đề xuất (sau)"}
COLOR = {"fifo": "#9e9e9e", "prio": "#1f77b4", "epq": "#d62728"}
MARK = {"fifo": "o", "prio": "s", "epq": "^"}
CLASSES = ["emergency", "critical", "best_effort"]
CLS_VI = {"emergency": "Khẩn cấp", "critical": "Video (critical)", "best_effort": "Best-effort"}
T95 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26}


def load_csv(path):
    with open(path) as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("load", "e_scale", "loss_pct", "throughput_kbps", "mean_delay_ms",
                  "p95_delay_ms", "jitter_ms"):
            r[k] = float(r[k])
    return rows


def agg(rows, key, metric):
    """Trả về {key: (mean, ci95)}."""
    g = defaultdict(list)
    for r in rows:
        g[key(r)].append(r[metric])
    out = {}
    for k, v in g.items():
        n = len(v); m = sum(v) / n
        sd = math.sqrt(sum((x - m) ** 2 for x in v) / (n - 1)) if n > 1 else 0
        out[k] = (m, T95.get(n - 1, 1.96) * sd / math.sqrt(n) if n > 1 else 0)
    return out


def line_plot(ax, rows, xkey, metric, cls, schemes=SCHEMES):
    sel = [r for r in rows if r["class"] == cls]
    a = agg(sel, lambda r: (r["scheme"], r[xkey]), metric)
    for s in schemes:
        xs = sorted(x for (sc, x) in a if sc == s)
        if not xs:
            continue
        ax.errorbar(xs, [a[(s, x)][0] for x in xs], yerr=[a[(s, x)][1] for x in xs],
                    label=LABEL[s], color=COLOR[s], marker=MARK[s], capsize=3, lw=1.8)
    ax.grid(alpha=.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--outdir", default="figures")
    ap.add_argument("--label", default="NS-3")
    ap.add_argument("--focus-load", type=float, default=1.2)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rows = load_csv(a.csv)
    A = [r for r in rows if r["experiment"] == "A"]
    B = [r for r in rows if r["experiment"] == "B"]
    Cx = [r for r in rows if r["experiment"].startswith("C_")]
    src = f"Nguồn: {a.label}"
    plt.rcParams.update({"font.size": 10, "figure.dpi": 130})

    # Hình 1 - RQ1: trễ khẩn cấp theo tải
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    line_plot(axs[0], A, "load", "mean_delay_ms", "emergency")
    axs[0].set(title="Trễ trung bình - dữ liệu khẩn cấp", xlabel="Tải đề nghị ρ", ylabel="ms")
    line_plot(axs[1], A, "load", "p95_delay_ms", "emergency")
    axs[1].set(title="Trễ phân vị 95 - dữ liệu khẩn cấp", xlabel="Tải đề nghị ρ", ylabel="ms")
    axs[0].legend(); fig.suptitle(f"RQ1: Trễ đầu-cuối của dữ liệu khẩn cấp ({src})")
    fig.tight_layout(); fig.savefig(f"{a.outdir}/fig1_emergency_delay_vs_load.png"); plt.close(fig)

    # Hình 2 - RQ1: mất gói & jitter khẩn cấp
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    line_plot(axs[0], A, "load", "loss_pct", "emergency")
    axs[0].set(title="Tỉ lệ mất gói - khẩn cấp", xlabel="Tải đề nghị ρ", ylabel="%")
    line_plot(axs[1], A, "load", "jitter_ms", "emergency")
    axs[1].set(title="Jitter - khẩn cấp", xlabel="Tải đề nghị ρ", ylabel="ms")
    axs[0].legend(); fig.suptitle(f"RQ1: Độ tin cậy dữ liệu khẩn cấp ({src})")
    fig.tight_layout(); fig.savefig(f"{a.outdir}/fig2_emergency_loss_jitter_vs_load.png"); plt.close(fig)

    # Hình 3 - TRƯỚC/SAU theo từng lớp tại tải tập trung
    sel = [r for r in A if abs(r["load"] - a.focus_load) < 1e-9]
    fig, axs = plt.subplots(1, 3, figsize=(14, 4))
    w = 0.26
    for ax, metric, title, unit in zip(
            axs, ["mean_delay_ms", "loss_pct", "throughput_kbps"],
            ["Trễ trung bình", "Tỉ lệ mất gói", "Thông lượng"], ["ms", "%", "kb/s"]):
        g = agg(sel, lambda r: (r["scheme"], r["class"]), metric)
        for j, s in enumerate(SCHEMES):
            xs = [i + (j - 1) * w for i in range(3)]
            vals = [g.get((s, c), (0, 0)) for c in CLASSES]
            bars = ax.bar(xs, [v[0] for v in vals], w, yerr=[v[1] for v in vals],
                          color=COLOR[s], label=LABEL[s], capsize=2)
            for b, v in zip(bars, vals):
                ax.annotate(f"{v[0]:.1f}", (b.get_x() + b.get_width() / 2, b.get_height()),
                            ha="center", va="bottom", fontsize=7)
        ax.set_xticks(range(3)); ax.set_xticklabels([CLS_VI[c] for c in CLASSES])
        ax.set(title=title, ylabel=unit); ax.grid(axis="y", alpha=.3)
    axs[0].legend(fontsize=8)
    fig.suptitle(f"So sánh TRƯỚC/SAU theo lớp lưu lượng tại ρ = {a.focus_load} ({src})")
    fig.tight_layout(); fig.savefig(f"{a.outdir}/fig3_before_after_per_class.png"); plt.close(fig)

    # Hình 4 - RQ2: bão khẩn cấp -> đói băng thông
    if B:
        fig, axs = plt.subplots(1, 3, figsize=(15, 4))
        for ax, cls in zip(axs, CLASSES):
            line_plot(ax, B, "e_scale", "throughput_kbps", cls)
            ax.set(title=f"Thông lượng {CLS_VI[cls]}", xlabel="Hệ số bão khẩn cấp (eScale)",
                   ylabel="kb/s")
        axs[0].legend(fontsize=8)
        fig.suptitle(f"RQ2: Hiện tượng đói băng thông khi lưu lượng khẩn cấp tăng vọt ({src})")
        fig.tight_layout(); fig.savefig(f"{a.outdir}/fig4_flood_starvation.png"); plt.close(fig)

        fig, ax = plt.subplots(figsize=(6, 4))
        line_plot(ax, B, "e_scale", "p95_delay_ms", "critical")
        ax.set(title=f"Trễ p95 của video khi bão khẩn cấp ({src})", xlabel="eScale", ylabel="ms")
        ax.set_yscale("log"); ax.legend(fontsize=8)
        fig.tight_layout(); fig.savefig(f"{a.outdir}/fig5_flood_video_delay.png"); plt.close(fig)

    # Hình 6 - độ nhạy tham số EmergencyRate
    if Cx:
        exps = sorted({r["experiment"] for r in Cx}, key=lambda e: int(e[2:-4]))
        fig, axs = plt.subplots(1, 2, figsize=(10, 4))
        for ax, metric, t in zip(axs, ["p95_delay_ms", "throughput_kbps"],
                                 ["Trễ p95 (ms)", "Thông lượng (kb/s)"]):
            for j, c in enumerate(CLASSES):
                g = agg([r for r in Cx if r["class"] == c], lambda r: r["experiment"], metric)
                xs = [i + (j - 1) * 0.26 for i in range(len(exps))]
                ax.bar(xs, [g[e][0] for e in exps], 0.26, yerr=[g[e][1] for e in exps],
                       label=CLS_VI[c], capsize=2)
            ax.set_xticks(range(len(exps))); ax.set_xticklabels([e[2:] for e in exps])
            ax.set(title=t, xlabel="EPQ EmergencyRate"); ax.grid(axis="y", alpha=.3)
        axs[0].set_yscale("log"); axs[1].legend(fontsize=8)
        fig.suptitle(f"Độ nhạy tham số EPQ (eScale=12, ρ=1.0) ({src})")
        fig.tight_layout(); fig.savefig(f"{a.outdir}/fig6_epq_sensitivity.png"); plt.close(fig)

    # Bảng tóm tắt
    lines = [f"# Tóm tắt kết quả ({a.label})\n",
             f"## Thí nghiệm A tại ρ = {a.focus_load}\n",
             "| Lớp | Chỉ số | FIFO | Strict Prio | EPQ | EPQ vs FIFO |",
             "|---|---|---|---|---|---|"]
    for c in CLASSES:
        for m, nm in [("mean_delay_ms", "Trễ TB (ms)"), ("p95_delay_ms", "Trễ p95 (ms)"),
                      ("loss_pct", "Mất gói (%)"), ("throughput_kbps", "Thông lượng (kb/s)")]:
            g = agg([r for r in sel if r["class"] == c], lambda r: r["scheme"], m)
            f, p, e = (g.get(s, (0, 0))[0] for s in SCHEMES)
            ch = f"{(e - f) / f * 100:+.1f}%" if f else "n/a"
            lines.append(f"| {CLS_VI[c]} | {nm} | {f:.2f} | {p:.2f} | {e:.2f} | {ch} |")
    if B:
        mx = max(r["e_scale"] for r in B)
        lines += [f"\n## Thí nghiệm B tại eScale = {mx:g} (bão khẩn cấp)\n",
                  "| Lớp | Thông lượng FIFO | Strict Prio | EPQ |", "|---|---|---|---|"]
        selB = [r for r in B if r["e_scale"] == mx]
        for c in CLASSES:
            g = agg([r for r in selB if r["class"] == c], lambda r: r["scheme"], "throughput_kbps")
            lines.append(f"| {CLS_VI[c]} | " + " | ".join(f"{g[s][0]:.0f}" for s in SCHEMES) + " |")
    with open(f"{a.outdir}/summary.md", "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
