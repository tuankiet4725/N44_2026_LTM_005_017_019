# Tóm tắt kết quả (NS-3)

## Thí nghiệm A tại ρ = 1.2

| Lớp | Chỉ số | FIFO | Strict Prio | EPQ | EPQ vs FIFO |
|---|---|---|---|---|---|
| Khẩn cấp | Trễ TB (ms) | 343.91 | 20.69 | 20.69 | -94.0% |
| Khẩn cấp | Trễ p95 (ms) | 387.90 | 23.50 | 23.50 | -93.9% |
| Khẩn cấp | Mất gói (%) | 31.04 | 0.00 | 0.00 | -100.0% |
| Khẩn cấp | Thông lượng (kb/s) | 127.49 | 184.92 | 184.92 | +45.0% |
| Video (critical) | Trễ TB (ms) | 355.92 | 25.64 | 31.15 | -91.2% |
| Video (critical) | Trễ p95 (ms) | 400.70 | 28.50 | 36.50 | -90.9% |
| Video (critical) | Mất gói (%) | 18.96 | 0.00 | 0.00 | -100.0% |
| Video (critical) | Thông lượng (kb/s) | 832.94 | 1027.86 | 1027.86 | +23.4% |
| Best-effort | Trễ TB (ms) | 350.99 | 381.23 | 722.56 | +105.9% |
| Best-effort | Trễ p95 (ms) | 399.70 | 459.30 | 784.30 | +96.2% |
| Best-effort | Mất gói (%) | 19.93 | 38.52 | 38.15 | +91.4% |
| Best-effort | Thông lượng (kb/s) | 1049.48 | 790.73 | 802.83 | -23.5% |

## Thí nghiệm B tại eScale = 20 (bão khẩn cấp)

| Lớp | Thông lượng FIFO | Strict Prio | EPQ |
|---|---|---|---|
| Khẩn cấp | 1512 | 1839 | 911 |
| Video (critical) | 433 | 136 | 1015 |
| Best-effort | 51 | 25 | 85 |
