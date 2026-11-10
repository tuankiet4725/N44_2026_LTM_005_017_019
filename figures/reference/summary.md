# Tóm tắt kết quả (mô hình tham chiếu Python)

## Thí nghiệm A tại ρ = 1.2

| Lớp | Chỉ số | FIFO | Strict Prio | EPQ | EPQ vs FIFO |
|---|---|---|---|---|---|
| Khẩn cấp | Trễ TB (ms) | 339.15 | 17.52 | 17.52 | -94.8% |
| Khẩn cấp | Trễ p95 (ms) | 387.59 | 19.37 | 19.37 | -95.0% |
| Khẩn cấp | Mất gói (%) | 36.30 | 0.00 | 0.00 | -100.0% |
| Khẩn cấp | Thông lượng (kb/s) | 109.21 | 171.47 | 171.47 | +57.0% |
| Video (critical) | Trễ TB (ms) | 354.12 | 21.24 | 26.55 | -92.5% |
| Video (critical) | Trễ p95 (ms) | 398.76 | 23.82 | 31.47 | -92.1% |
| Video (critical) | Mất gói (%) | 27.81 | 0.00 | 0.00 | -100.0% |
| Video (critical) | Thông lượng (kb/s) | 721.93 | 999.97 | 999.97 | +38.5% |
| Best-effort | Trễ TB (ms) | 354.07 | 383.31 | 654.06 | +84.7% |
| Best-effort | Trễ p95 (ms) | 398.80 | 464.22 | 702.40 | +76.1% |
| Best-effort | Mất gói (%) | 27.73 | 50.67 | 50.11 | +80.7% |
| Best-effort | Thông lượng (kb/s) | 1113.63 | 760.14 | 768.73 | -31.0% |

## Thí nghiệm B tại eScale = 20 (bão khẩn cấp)

| Lớp | Thông lượng FIFO | Strict Prio | EPQ |
|---|---|---|---|
| Khẩn cấp | 1270 | 1640 | 796 |
| Video (critical) | 406 | 130 | 948 |
| Best-effort | 135 | 7 | 125 |
