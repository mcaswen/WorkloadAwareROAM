# FER-02 数表与图像附录

日期：2026-09-16。解释与限制见[主报告](fer_02_overall_results.md)。本页由同一分析产物归约；缺失以—表示，不补零。

## A. 配置身份与参考域

同资产的输入样本、相机和参考Q冻结；CPU预算与CBT动态槽容量分开。完整32个case及随机执行顺序在[协议](../../../configs/experiments/formal/fer_02/protocol.json)。

| 资产 | 栅格 | Size / HeightScale | 路线 | Q | sample SHA256 |
|---|---|---|---|---|---|
| peking547 | 547×547 | 80.000 / 12.000000000 | pq-return24 | 1790881 | aa8d08125e409ef497e2553319d62bb2c2a32fe7b5cb4b90b0f18f79238a914f |
| generated-ridge | 513×513 | 80.000 / 12.000000000 | overview-orbit | 1574913 | 89c310713046ffdb4724df0032a28f3ca852ccb503479287392717fcc81c7fa2 |
| dem-canyon | 513×513 | 80.000 / 11.973810547 | reveal-return | 1574913 | e1e8da6c861e3ec8c451aec601e569c6ab756a6e757a6eff27be7cea5efd7cf2 |
| dem-sierra | 513×513 | 80.000 / 10.389419922 | reveal-return | 1574913 | ffd5787ebf7b451fc306b7a9d447917010ff39bceac3e19ce6aaa524fa67a70b |


## B. 正常独立进程完整成本

三个值按r1/r2/r3排列。CPU表为完整更新；GPU表主机录制不能当设备完成时间。3个进程是重复单位；帧P95仅作单轨迹描述。

| 配置 | CPU三值ms | 平均CPUms | 帧包络ms | 上传ms | 上传字节/暖机会 |
|---|---|---|---|---|---|
| dem-canyon-b50000-classic-t1 | 45.410 / 45.540 / 43.355 | 44.768 | 45.836 | 0.330 | 860210.6 |
| dem-canyon-b50000-dod-t8 | 19.297 / 18.974 / 20.291 | 19.521 | 20.643 | 0.393 | 941061.9 |
| dem-canyon-b50000-transactional-t1 | 88.430 / 88.463 / 89.075 | 88.656 | 89.444 | 0.008 | 1870.8 |
| dem-canyon-b50000-transactional-t8 | 17.977 / 17.797 / 16.878 | 17.551 | 18.249 | 0.006 | 1870.8 |
| dem-sierra-b50000-classic-t1 | 8.269 / 6.792 / 7.389 | 7.483 | 8.725 | 0.789 | 758987.9 |
| dem-sierra-b50000-dod-t8 | 2.410 / 2.267 / 2.492 | 2.390 | 3.434 | 0.715 | 758608.5 |
| dem-sierra-b50000-transactional-t8 | 11.531 / 11.818 / 11.862 | 11.737 | 12.691 | 0.406 | 274920.3 |
| generated-ridge-b50000-classic-t1 | 4.094 / 3.470 / 3.588 | 3.717 | 5.266 | 1.175 | 856507.4 |
| generated-ridge-b50000-dod-t8 | 1.427 / 1.640 / 1.421 | 1.496 | 2.862 | 1.069 | 856507.4 |
| generated-ridge-b50000-transactional-t8 | 34.769 / 35.945 / 35.150 | 35.288 | 37.220 | 1.275 | 577643.6 |
| peking547-b100000-classic-t1 | 225.882 / 235.179 / 228.166 | 229.743 | 232.180 | 1.542 | 10615168.0 |
| peking547-b100000-dod-t8 | 138.266 / 152.393 / 138.136 | 142.932 | 146.293 | 2.417 | 11801688.0 |
| peking547-b100000-transactional-t8 | 62.191 / 60.999 / 61.371 | 61.520 | 62.269 | 0.016 | 18137.1 |
| peking547-b200000-classic-t1 | 490.535 / 475.890 / 492.239 | 486.221 | 490.561 | 3.264 | 21026152.0 |
| peking547-b200000-dod-t8 | 343.108 / 329.214 / 315.704 | 329.342 | 334.245 | 3.797 | 21033264.0 |
| peking547-b200000-transactional-t8 | 502.470 / 501.173 / 500.765 | 501.469 | 502.346 | 0.040 | 53092.0 |
| peking547-b50000-classic-t1 | 107.803 / 110.034 / 102.074 | 106.637 | 108.249 | 0.791 | 5261800.0 |
| peking547-b50000-dod-t8 | 58.754 / 65.284 / 56.260 | 60.099 | 62.206 | 1.273 | 5852176.0 |
| peking547-b50000-transactional-t1 | 129.075 / 128.032 / 128.425 | 128.511 | 129.361 | 0.015 | 14690.9 |
| peking547-b50000-transactional-t8 | 37.704 / 38.150 / 37.590 | 37.815 | 38.536 | 0.015 | 14690.9 |

| 配置 | GPU计算三值ms | 平均GPU计算ms | GPU绘制ms | 主机录制ms | 主机帧包络ms |
|---|---|---|---|---|---|
| dem-canyon-cbt-a16 | 0.07568 / 0.08236 / 0.07537 | 0.07780 | 0.01292 | 0.03740 | 0.61791 |
| dem-canyon-cbt-a4 | 0.08644 / 0.09114 / 0.09189 | 0.08982 | 0.02513 | 0.03906 | 0.48401 |
| dem-canyon-cbt-a8 | 0.08418 / 0.08273 / 0.08174 | 0.08288 | 0.01802 | 0.03726 | 0.57473 |
| dem-sierra-cbt-a16 | 0.06813 / 0.07280 / 0.06742 | 0.06945 | 0.01200 | 0.03544 | 0.45917 |
| dem-sierra-cbt-a4 | 0.07886 / 0.08549 / 0.08172 | 0.08202 | 0.02359 | 0.03717 | 0.49542 |
| dem-sierra-cbt-a8 | 0.07008 / 0.07555 / 0.07005 | 0.07189 | 0.01630 | 0.03696 | 0.64419 |
| generated-ridge-cbt-a16 | 0.07826 / 0.08189 / 0.08359 | 0.08125 | 0.01382 | 0.03614 | 0.51906 |
| generated-ridge-cbt-a4 | 0.08756 / 0.09289 / 0.08730 | 0.08925 | 0.02887 | 0.03989 | 0.63436 |
| generated-ridge-cbt-a8 | 0.08171 / 0.08385 / 0.08652 | 0.08403 | 0.01956 | 0.03752 | 0.59499 |
| peking547-cbt-a16 | 0.08752 / 0.08685 / 0.07817 | 0.08418 | 0.01414 | 0.03971 | 0.43149 |
| peking547-cbt-a4 | 0.10745 / 0.10717 / 0.09569 | 0.10344 | 0.02292 | 0.03863 | 0.48911 |
| peking547-cbt-a8 | 0.08715 / 0.09345 / 0.09201 | 0.09087 | 0.01854 | 0.03783 | 0.46227 |


## C. 冷启动、移动与返回成本

CPU为完整CPU更新，CBT为GPU计算（第0机会无有效来源代时留空）；不能跨设备称同任务加速。分组由原冻结事件标签确定，空分组不补值。

| 配置 | 边界 | 第0机会ms | 移动ms | 返回/恢复ms | 静止ms |
|---|---|---|---|---|---|
| dem-canyon-b50000-classic-t1 | CPU | 324.423 | 43.406 | 45.386 | — |
| dem-canyon-b50000-dod-t8 | CPU | 217.560 | 20.783 | 18.948 | — |
| dem-canyon-b50000-transactional-t1 | CPU | 4150.288 | 127.618 | 71.001 | — |
| dem-canyon-b50000-transactional-t8 | CPU | 4101.101 | 26.661 | 13.423 | — |
| dem-canyon-cbt-a16 | GPU compute | 0.062 | 0.079 | 0.077 | — |
| dem-canyon-cbt-a4 | GPU compute | 0.062 | 0.093 | 0.088 | — |
| dem-canyon-cbt-a8 | GPU compute | 0.063 | 0.085 | 0.082 | — |
| dem-sierra-b50000-classic-t1 | CPU | 128.096 | 4.568 | 8.804 | — |
| dem-sierra-b50000-dod-t8 | CPU | 115.334 | 2.145 | 2.501 | — |
| dem-sierra-b50000-transactional-t8 | CPU | 1429.568 | 17.238 | 9.244 | — |
| dem-sierra-cbt-a16 | GPU compute | 0.062 | 0.071 | 0.069 | — |
| dem-sierra-cbt-a4 | GPU compute | 0.062 | 0.085 | 0.081 | — |
| dem-sierra-cbt-a8 | GPU compute | 0.063 | 0.075 | 0.071 | — |
| generated-ridge-b50000-classic-t1 | CPU | 113.132 | 3.725 | 3.038 | — |
| generated-ridge-b50000-dod-t8 | CPU | 109.004 | 1.500 | 1.121 | — |
| generated-ridge-b50000-transactional-t8 | CPU | 1104.564 | 35.548 | 11.378 | — |
| generated-ridge-cbt-a16 | GPU compute | 0.062 | 0.081 | — | — |
| generated-ridge-cbt-a4 | GPU compute | 0.063 | 0.089 | — | — |
| generated-ridge-cbt-a8 | GPU compute | 0.063 | 0.084 | — | — |
| peking547-b100000-classic-t1 | CPU | 580.331 | 271.210 | 162.359 | — |
| peking547-b100000-dod-t8 | CPU | 352.446 | 178.428 | 85.249 | — |
| peking547-b100000-transactional-t8 | CPU | 2967.258 | 94.171 | 8.463 | — |
| peking547-b200000-classic-t1 | CPU | 1100.055 | 581.793 | 330.918 | — |
| peking547-b200000-dod-t8 | CPU | 743.843 | 406.876 | 203.349 | — |
| peking547-b200000-transactional-t8 | CPU | 5821.293 | 800.342 | 15.801 | — |
| peking547-b50000-classic-t1 | CPU | 306.838 | 127.504 | 72.728 | — |
| peking547-b50000-dod-t8 | CPU | 214.979 | 74.935 | 35.991 | — |
| peking547-b50000-transactional-t1 | CPU | 1656.197 | 189.993 | 28.601 | — |
| peking547-b50000-transactional-t8 | CPU | 1622.383 | 57.795 | 5.347 | — |
| peking547-cbt-a16 | GPU compute | 0.063 | 0.080 | 0.094 | — |
| peking547-cbt-a4 | GPU compute | 0.063 | 0.090 | 0.132 | — |
| peking547-cbt-a8 | GPU compute | 0.063 | 0.083 | 0.107 | — |


## D. 事务工作量和翻边续接

以下取固定r1全轨迹，其他两重复工作计数已核对。raw/need等为原公开字段；exchanges不含free refinement和净零翻边。表中总事务帧/最大总批宽由独立flip-recovery.csv补入。

| 配置 | raw | examined | receiver | need | feasible | exchange | free |
|---|---|---|---|---|---|---|---|
| dem-canyon-b50000-transactional-t8 | 390996 | 15360 | 165 | 165 | 163 | 77 | 0 |
| dem-sierra-b50000-transactional-t8 | 217269 | 15008 | 169 | 0 | 0 | 0 | 92 |
| generated-ridge-b50000-transactional-t8 | 65913 | 14548 | 246 | 0 | 0 | 0 | 152 |
| peking547-b100000-transactional-t8 | 2304963 | 7680 | 412 | 412 | 412 | 173 | 0 |
| peking547-b200000-transactional-t8 | 4368289 | 15360 | 1114 | 1114 | 1114 | 522 | 0 |
| peking547-b50000-transactional-t8 | 1190653 | 3840 | 308 | 308 | 308 | 145 | 0 |

| 配置 | PairChecks | conflicts | donor reuse | evaluations | 翻边尝试/执行 | 总事务帧 | 最大总批宽 |
|---|---|---|---|---|---|---|---|
| dem-canyon-b50000-transactional-t8 | 15154 | 8843 | 491 | 102431196 | 1704 / 1 | 20 | 12 |
| dem-sierra-b50000-transactional-t8 | 0 | 77 | 0 | 102421927 | 2620 / 2 | 16 | 21 |
| generated-ridge-b50000-transactional-t8 | 0 | 94 | 0 | 151364864 | 0 / 0 | 49 | 9 |
| peking547-b100000-transactional-t8 | 79672 | 69987 | 5327 | 26937776 | 292 / 1 | 10 | 46 |
| peking547-b200000-transactional-t8 | 412142 | 355191 | 48240 | 26983753 | 670 / 1 | 12 | 150 |
| peking547-b50000-transactional-t8 | 28580 | 23551 | 4127 | 26960324 | 244 / 1 | 11 | 41 |


## E. 全部120个质量观察

N是对应捕获网格的实际三角形数。Emax/RMS为px，Hmax为世界高度单位；RMS为参数域等权。无效点保留N和歧义计数，但不列部分误差。不是连续曲面的数学最大值。

### peking547

| 配置 | 机会 | N | Emax | RMS | Hmax | 歧义 | 状态 |
|---|---|---|---|---|---|---|---|
| b100000-classic-t1 | 2 | 100000 | 0.330869 | 0.020319 | 0.167582 | 0 | ok |
| b100000-classic-t1 | 15 | 100000 | 0.349195 | 0.019292 | 0.281969 | 0 | ok |
| b100000-classic-t1 | 16 | 100000 | 0.330869 | 0.020320 | 0.167582 | 0 | ok |
| b100000-classic-t1 | 23 | 100000 | 0.330869 | 0.020319 | 0.167582 | 0 | ok |
| b100000-dod-t8 | 2 | 99999 | 0.330869 | 0.020320 | 0.167582 | 0 | ok |
| b100000-dod-t8 | 15 | 100000 | 0.349195 | 0.019292 | 0.281969 | 0 | ok |
| b100000-dod-t8 | 16 | 99999 | 0.330869 | 0.020320 | 0.167582 | 0 | ok |
| b100000-dod-t8 | 23 | 99999 | 0.330869 | 0.020320 | 0.167582 | 0 | ok |
| b100000-transactional-t8 | 2 | 99999 | 0.330869 | 0.020320 | 0.167582 | 0 | ok |
| b100000-transactional-t8 | 15 | 99999 | 1.996557 | 0.105261 | 0.167582 | 0 | ok |
| b100000-transactional-t8 | 16 | 99999 | 0.330869 | 0.020293 | 0.167582 | 0 | ok |
| b100000-transactional-t8 | 23 | 99999 | 0.330869 | 0.020293 | 0.167582 | 0 | ok |
| b200000-classic-t1 | 2 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-classic-t1 | 15 | 200000 | 0.193772 | 0.010453 | 0.281969 | 0 | ok |
| b200000-classic-t1 | 16 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-classic-t1 | 23 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-dod-t8 | 2 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-dod-t8 | 15 | 199999 | 0.193772 | 0.010453 | 0.281969 | 0 | ok |
| b200000-dod-t8 | 16 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-dod-t8 | 23 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-transactional-t8 | 2 | 200000 | 0.209150 | 0.011188 | 0.167582 | 0 | ok |
| b200000-transactional-t8 | 15 | 200000 | 1.996557 | 0.099599 | 0.167582 | 0 | ok |
| b200000-transactional-t8 | 16 | 200000 | 0.209150 | 0.011031 | 0.167582 | 0 | ok |
| b200000-transactional-t8 | 23 | 200000 | 0.209150 | 0.011031 | 0.167582 | 0 | ok |
| b50000-classic-t1 | 2 | 50000 | 0.441886 | 0.035832 | 0.167582 | 0 | ok |
| b50000-classic-t1 | 15 | 50000 | 0.418727 | 0.034052 | 0.281969 | 0 | ok |
| b50000-classic-t1 | 16 | 50000 | 0.441886 | 0.035832 | 0.167582 | 0 | ok |
| b50000-classic-t1 | 23 | 50000 | 0.441886 | 0.035823 | 0.167582 | 0 | ok |
| b50000-dod-t8 | 2 | 49999 | 0.441886 | 0.035832 | 0.167582 | 0 | ok |
| b50000-dod-t8 | 15 | 50000 | 0.418727 | 0.034052 | 0.281969 | 0 | ok |
| b50000-dod-t8 | 16 | 49999 | 0.441886 | 0.035832 | 0.167582 | 0 | ok |
| b50000-dod-t8 | 23 | 49999 | 0.441886 | 0.035832 | 0.167582 | 0 | ok |
| b50000-transactional-t8 | 2 | 49999 | 0.441886 | 0.035832 | 0.167582 | 0 | ok |
| b50000-transactional-t8 | 15 | 49999 | 1.996557 | 0.115327 | 0.167582 | 0 | ok |
| b50000-transactional-t8 | 16 | 49999 | 0.441886 | 0.035622 | 0.167582 | 0 | ok |
| b50000-transactional-t8 | 23 | 49999 | 0.441886 | 0.035622 | 0.167582 | 0 | ok |
| cbt-a16 | 2 | 48 | 8.091671 | 2.096283 | 0.782442 | 0 | ok |
| cbt-a16 | 15 | 45112 | 1.069818 | 0.064205 | 0.115252 | 0 | ok |
| cbt-a16 | 16 | 34582 | 0.927970 | 0.062479 | 0.119879 | 0 | ok |
| cbt-a16 | 23 | 45844 | 0.927970 | 0.052939 | 0.244752 | 0 | ok |
| cbt-a4 | 2 | 48 | 8.091671 | 2.096283 | 0.782442 | 0 | ok |
| cbt-a4 | 15 | 130269 | 1.069818 | 0.038599 | 0.115252 | 0 | ok |
| cbt-a4 | 16 | 110004 | 0.653945 | 0.025893 | 0.112152 | 0 | ok |
| cbt-a4 | 23 | 140913 | 0.407000 | 0.018077 | 0.167582 | 0 | ok |
| cbt-a8 | 2 | 48 | 8.091671 | 2.096283 | 0.782442 | 0 | ok |
| cbt-a8 | 15 | 80193 | 1.069818 | 0.046626 | 0.115252 | 0 | ok |
| cbt-a8 | 16 | 63132 | 0.758890 | 0.039918 | 0.112152 | 0 | ok |
| cbt-a8 | 23 | 85238 | 0.758890 | 0.030922 | 0.244752 | 0 | ok |

### generated-ridge

| 配置 | 机会 | N | Emax | RMS | Hmax | 歧义 | 状态 |
|---|---|---|---|---|---|---|---|
| b50000-classic-t1 | 2 | 5553 | 1.064190 | 0.157245 | 0.176974 | 0 | ok |
| b50000-classic-t1 | 48 | 7999 | 1.036579 | 0.120857 | 0.160265 | 0 | ok |
| b50000-classic-t1 | 80 | 8458 | 0.989473 | 0.105711 | 0.160265 | 0 | ok |
| b50000-classic-t1 | 95 | 8264 | 0.971799 | 0.106970 | 0.160265 | 0 | ok |
| b50000-dod-t8 | 2 | 5553 | 1.064190 | 0.157245 | 0.176974 | 0 | ok |
| b50000-dod-t8 | 48 | 7999 | 1.036579 | 0.120857 | 0.160265 | 0 | ok |
| b50000-dod-t8 | 80 | 8458 | 0.989473 | 0.105711 | 0.160265 | 0 | ok |
| b50000-dod-t8 | 95 | 8264 | 0.971799 | 0.106970 | 0.160265 | 0 | ok |
| b50000-transactional-t8 | 2 | 5467 | 1.064190 | 0.160026 | 0.176974 | 0 | ok |
| b50000-transactional-t8 | 48 | 5593 | 1.163989 | 0.163679 | 0.176974 | 0 | ok |
| b50000-transactional-t8 | 80 | 5655 | 1.131319 | 0.159519 | 0.176974 | 0 | ok |
| b50000-transactional-t8 | 95 | 5747 | 1.059956 | 0.159144 | 0.176974 | 0 | ok |
| cbt-a16 | 2 | 48 | 48.631061 | 9.367309 | 7.373926 | 0 | ok |
| cbt-a16 | 48 | 25662 | 2.629353 | 0.204794 | 0.447736 | 0 | ok |
| cbt-a16 | 80 | 24990 | 2.530013 | 0.192879 | 0.489726 | 0 | ok |
| cbt-a16 | 95 | 25574 | 4.148093 | 0.221768 | 0.682227 | 0 | ok |
| cbt-a4 | 2 | 48 | 48.631061 | 9.367309 | 7.373926 | 0 | ok |
| cbt-a4 | 48 | 101264 | 1.518783 | 0.094452 | 0.268292 | 0 | ok |
| cbt-a4 | 80 | 98461 | 2.054890 | 0.102797 | 0.397997 | 0 | ok |
| cbt-a4 | 95 | 100869 | 2.314244 | 0.129428 | 0.397997 | 0 | ok |
| cbt-a8 | 2 | 48 | 48.631061 | 9.367309 | 7.373926 | 0 | ok |
| cbt-a8 | 48 | 50943 | 2.141228 | 0.132790 | 0.355788 | 0 | ok |
| cbt-a8 | 80 | 49577 | 2.306863 | 0.138607 | 0.446567 | 0 | ok |
| cbt-a8 | 95 | 50744 | 2.893437 | 0.152964 | 0.484921 | 0 | ok |

### dem-canyon

| 配置 | 机会 | N | Emax | RMS | Hmax | 歧义 | 状态 |
|---|---|---|---|---|---|---|---|
| b50000-classic-t1 | 2 | 50000 | 2.315694 | 0.298090 | 1.165875 | 0 | ok |
| b50000-classic-t1 | 48 | 49999 | 1.839557 | 0.245676 | 6.259915 | 0 | ok |
| b50000-classic-t1 | 80 | 50000 | 2.348193 | 0.299029 | 1.165875 | 0 | ok |
| b50000-classic-t1 | 95 | 50000 | 2.348193 | 0.299029 | 1.165875 | 0 | ok |
| b50000-dod-t8 | 2 | 49996 | 2.315694 | 0.298117 | 1.165875 | 0 | ok |
| b50000-dod-t8 | 48 | 49999 | 1.839557 | 0.245676 | 6.259915 | 0 | ok |
| b50000-dod-t8 | 80 | 50000 | 2.348193 | 0.299029 | 1.165875 | 0 | ok |
| b50000-dod-t8 | 95 | 50000 | 2.348193 | 0.299029 | 1.165875 | 0 | ok |
| b50000-transactional-t8 | 2 | 50000 | 4.636925 | 0.302821 | 1.165875 | 0 | ok |
| b50000-transactional-t8 | 48 | 50000 | 9.274477 | 0.479200 | 1.165875 | 0 | ok |
| b50000-transactional-t8 | 80 | 50000 | 2.348193 | 0.299160 | 1.165875 | 0 | ok |
| b50000-transactional-t8 | 95 | 50000 | 2.348193 | 0.299160 | 1.165875 | 0 | ok |
| cbt-a16 | 2 | 48 | 63.068155 | 10.541334 | 6.420384 | 0 | ok |
| cbt-a16 | 48 | 26716 | 11.115904 | 0.813822 | 4.758042 | 0 | ok |
| cbt-a16 | 80 | 34973 | 9.069726 | 0.662774 | 2.498004 | 0 | ok |
| cbt-a16 | 95 | 34973 | 9.069726 | 0.662754 | 2.498004 | 0 | ok |
| cbt-a4 | 2 | 48 | 63.068155 | 10.541334 | 6.420384 | 0 | ok |
| cbt-a4 | 48 | 87729 | — | — | — | 104 | invalid-quality |
| cbt-a4 | 80 | 125405 | — | — | — | 123 | invalid-quality |
| cbt-a4 | 95 | 125407 | — | — | — | 123 | invalid-quality |
| cbt-a8 | 2 | 48 | 63.068155 | 10.541334 | 6.420384 | 0 | ok |
| cbt-a8 | 48 | 49165 | — | — | — | 1 | invalid-quality |
| cbt-a8 | 80 | 67261 | — | — | — | 11 | invalid-quality |
| cbt-a8 | 95 | 67255 | — | — | — | 11 | invalid-quality |

### dem-sierra

| 配置 | 机会 | N | Emax | RMS | Hmax | 歧义 | 状态 |
|---|---|---|---|---|---|---|---|
| b50000-classic-t1 | 2 | 8845 | 1.172692 | 0.253809 | 0.470792 | 0 | ok |
| b50000-classic-t1 | 48 | 12085 | 1.041680 | 0.188354 | 1.754270 | 0 | ok |
| b50000-classic-t1 | 80 | 15919 | 1.089577 | 0.178018 | 0.377723 | 0 | ok |
| b50000-classic-t1 | 95 | 15919 | 1.089577 | 0.178018 | 0.377723 | 0 | ok |
| b50000-dod-t8 | 2 | 8845 | 1.172692 | 0.253809 | 0.470792 | 0 | ok |
| b50000-dod-t8 | 48 | 12085 | 1.041680 | 0.188354 | 1.754270 | 0 | ok |
| b50000-dod-t8 | 80 | 15919 | 1.089577 | 0.178018 | 0.377723 | 0 | ok |
| b50000-dod-t8 | 95 | 15919 | 1.089577 | 0.178018 | 0.377723 | 0 | ok |
| b50000-transactional-t8 | 2 | 8874 | 1.586408 | 0.257337 | 0.377723 | 0 | ok |
| b50000-transactional-t8 | 48 | 8960 | 2.015012 | 0.288285 | 0.377723 | 0 | ok |
| b50000-transactional-t8 | 80 | 8976 | 1.133568 | 0.254938 | 0.377723 | 0 | ok |
| b50000-transactional-t8 | 95 | 8976 | 1.133568 | 0.254938 | 0.377723 | 0 | ok |
| cbt-a16 | 2 | 48 | 16.374504 | 3.691854 | 2.285105 | 0 | ok |
| cbt-a16 | 48 | 22496 | 3.189845 | 0.217448 | 1.327550 | 0 | ok |
| cbt-a16 | 80 | 28643 | 1.742150 | 0.201018 | 0.508503 | 0 | ok |
| cbt-a16 | 95 | 28643 | 1.742150 | 0.201018 | 0.508503 | 0 | ok |
| cbt-a4 | 2 | 48 | 16.374504 | 3.691854 | 2.285105 | 0 | ok |
| cbt-a4 | 48 | 76472 | — | — | — | 38 | invalid-quality |
| cbt-a4 | 80 | 110369 | — | — | — | 11 | invalid-quality |
| cbt-a4 | 95 | 110369 | — | — | — | 11 | invalid-quality |
| cbt-a8 | 2 | 48 | 16.374504 | 3.691854 | 2.285105 | 0 | ok |
| cbt-a8 | 48 | 41522 | 3.189845 | 0.176214 | 1.327550 | 0 | ok |
| cbt-a8 | 80 | 56100 | 1.489559 | 0.126481 | 0.508503 | 0 | ok |
| cbt-a8 | 95 | 56100 | 1.489559 | 0.126481 | 0.508503 | 0 | ok |


## F. 全部逐点配对

Dmax=max(e_candidate(q)−e_reference(q))，正值表示至少一个采样点更差；不是Emax相减。配对目录名去掉quality-前缀。N不同仍保留，不称等价质量/工作量。

### peking547

| 候选 | 参考 | 机会 | 候选N | 参考N | Dmax px | 状态 |
|---|---|---|---|---|---|---|
| b100000-classic-t1 | b100000-dod-t8 | 2 | 100000 | 99999 | 0.003755 | paired |
| b100000-classic-t1 | b100000-dod-t8 | 15 | 100000 | 100000 | 0.000000 | paired |
| b100000-classic-t1 | b100000-dod-t8 | 16 | 100000 | 99999 | 0.000000 | paired |
| b100000-classic-t1 | b100000-dod-t8 | 23 | 100000 | 99999 | 0.003755 | paired |
| b100000-transactional-t8 | b100000-dod-t8 | 2 | 99999 | 99999 | 0.000000 | paired |
| b100000-transactional-t8 | b100000-dod-t8 | 15 | 99999 | 100000 | 1.977369 | paired |
| b100000-transactional-t8 | b100000-dod-t8 | 16 | 99999 | 99999 | 0.162941 | paired |
| b100000-transactional-t8 | b100000-dod-t8 | 23 | 99999 | 99999 | 0.162941 | paired |
| b100000-transactional-t8 | cbt-a16 | 2 | 99999 | 48 | 0.227526 | paired |
| b100000-transactional-t8 | cbt-a16 | 15 | 99999 | 45112 | 1.903046 | paired |
| b100000-transactional-t8 | cbt-a16 | 16 | 99999 | 34582 | 0.247713 | paired |
| b100000-transactional-t8 | cbt-a16 | 23 | 99999 | 45844 | 0.242371 | paired |
| b100000-transactional-t8 | cbt-a4 | 2 | 99999 | 48 | 0.227526 | paired |
| b100000-transactional-t8 | cbt-a4 | 15 | 99999 | 130269 | 1.971957 | paired |
| b100000-transactional-t8 | cbt-a4 | 16 | 99999 | 110004 | 0.327507 | paired |
| b100000-transactional-t8 | cbt-a4 | 23 | 99999 | 140913 | 0.327507 | paired |
| b100000-transactional-t8 | cbt-a8 | 2 | 99999 | 48 | 0.227526 | paired |
| b100000-transactional-t8 | cbt-a8 | 15 | 99999 | 80193 | 1.969849 | paired |
| b100000-transactional-t8 | cbt-a8 | 16 | 99999 | 63132 | 0.284264 | paired |
| b100000-transactional-t8 | cbt-a8 | 23 | 99999 | 85238 | 0.243128 | paired |
| b200000-classic-t1 | b200000-dod-t8 | 2 | 200000 | 200000 | 0.000000 | paired |
| b200000-classic-t1 | b200000-dod-t8 | 15 | 200000 | 199999 | 0.000621 | paired |
| b200000-classic-t1 | b200000-dod-t8 | 16 | 200000 | 200000 | 0.000000 | paired |
| b200000-classic-t1 | b200000-dod-t8 | 23 | 200000 | 200000 | 0.000000 | paired |
| b200000-transactional-t8 | b200000-dod-t8 | 2 | 200000 | 200000 | 0.000000 | paired |
| b200000-transactional-t8 | b200000-dod-t8 | 15 | 200000 | 199999 | 1.991872 | paired |
| b200000-transactional-t8 | b200000-dod-t8 | 16 | 200000 | 200000 | 0.074878 | paired |
| b200000-transactional-t8 | b200000-dod-t8 | 23 | 200000 | 200000 | 0.074878 | paired |
| b200000-transactional-t8 | cbt-a16 | 2 | 200000 | 48 | 0.159990 | paired |
| b200000-transactional-t8 | cbt-a16 | 15 | 200000 | 45112 | 1.903046 | paired |
| b200000-transactional-t8 | cbt-a16 | 16 | 200000 | 34582 | 0.189539 | paired |
| b200000-transactional-t8 | cbt-a16 | 23 | 200000 | 45844 | 0.166034 | paired |
| b200000-transactional-t8 | cbt-a4 | 2 | 200000 | 48 | 0.159990 | paired |
| b200000-transactional-t8 | cbt-a4 | 15 | 200000 | 130269 | 1.971957 | paired |
| b200000-transactional-t8 | cbt-a4 | 16 | 200000 | 110004 | 0.191053 | paired |
| b200000-transactional-t8 | cbt-a4 | 23 | 200000 | 140913 | 0.188822 | paired |
| b200000-transactional-t8 | cbt-a8 | 2 | 200000 | 48 | 0.159990 | paired |
| b200000-transactional-t8 | cbt-a8 | 15 | 200000 | 80193 | 1.969849 | paired |
| b200000-transactional-t8 | cbt-a8 | 16 | 200000 | 63132 | 0.190789 | paired |
| b200000-transactional-t8 | cbt-a8 | 23 | 200000 | 85238 | 0.188822 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 2 | 50000 | 49999 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 15 | 50000 | 50000 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 16 | 50000 | 49999 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 23 | 50000 | 49999 | 0.004590 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 2 | 49999 | 49999 | 0.000000 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 15 | 49999 | 50000 | 1.977369 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 16 | 49999 | 49999 | 0.101125 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 23 | 49999 | 49999 | 0.101125 | paired |
| b50000-transactional-t8 | cbt-a16 | 2 | 49999 | 48 | 0.323098 | paired |
| b50000-transactional-t8 | cbt-a16 | 15 | 49999 | 45112 | 1.903046 | paired |
| b50000-transactional-t8 | cbt-a16 | 16 | 49999 | 34582 | 0.389818 | paired |
| b50000-transactional-t8 | cbt-a16 | 23 | 49999 | 45844 | 0.328234 | paired |
| b50000-transactional-t8 | cbt-a4 | 2 | 49999 | 48 | 0.323098 | paired |
| b50000-transactional-t8 | cbt-a4 | 15 | 49999 | 130269 | 1.971957 | paired |
| b50000-transactional-t8 | cbt-a4 | 16 | 49999 | 110004 | 0.389818 | paired |
| b50000-transactional-t8 | cbt-a4 | 23 | 49999 | 140913 | 0.389818 | paired |
| b50000-transactional-t8 | cbt-a8 | 2 | 49999 | 48 | 0.323098 | paired |
| b50000-transactional-t8 | cbt-a8 | 15 | 49999 | 80193 | 1.969849 | paired |
| b50000-transactional-t8 | cbt-a8 | 16 | 49999 | 63132 | 0.389818 | paired |
| b50000-transactional-t8 | cbt-a8 | 23 | 49999 | 85238 | 0.389818 | paired |

### generated-ridge

| 候选 | 参考 | 机会 | 候选N | 参考N | Dmax px | 状态 |
|---|---|---|---|---|---|---|
| b50000-classic-t1 | b50000-dod-t8 | 2 | 5553 | 5553 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 48 | 7999 | 7999 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 80 | 8458 | 8458 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 95 | 8264 | 8264 | 0.000000 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 2 | 5467 | 5553 | 1.000953 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 48 | 5593 | 7999 | 1.163987 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 80 | 5655 | 8458 | 1.131315 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 95 | 5747 | 8264 | 1.059955 | paired |
| b50000-transactional-t8 | cbt-a16 | 2 | 5467 | 48 | 0.854784 | paired |
| b50000-transactional-t8 | cbt-a16 | 48 | 5593 | 25662 | 1.083708 | paired |
| b50000-transactional-t8 | cbt-a16 | 80 | 5655 | 24990 | 1.131315 | paired |
| b50000-transactional-t8 | cbt-a16 | 95 | 5747 | 25574 | 0.998961 | paired |
| b50000-transactional-t8 | cbt-a4 | 2 | 5467 | 48 | 0.854784 | paired |
| b50000-transactional-t8 | cbt-a4 | 48 | 5593 | 101264 | 1.163987 | paired |
| b50000-transactional-t8 | cbt-a4 | 80 | 5655 | 98461 | 1.131315 | paired |
| b50000-transactional-t8 | cbt-a4 | 95 | 5747 | 100869 | 1.009703 | paired |
| b50000-transactional-t8 | cbt-a8 | 2 | 5467 | 48 | 0.854784 | paired |
| b50000-transactional-t8 | cbt-a8 | 48 | 5593 | 50943 | 1.163987 | paired |
| b50000-transactional-t8 | cbt-a8 | 80 | 5655 | 49577 | 1.131315 | paired |
| b50000-transactional-t8 | cbt-a8 | 95 | 5747 | 50744 | 0.998962 | paired |

### dem-canyon

| 候选 | 参考 | 机会 | 候选N | 参考N | Dmax px | 状态 |
|---|---|---|---|---|---|---|
| b50000-classic-t1 | b50000-dod-t8 | 2 | 50000 | 49996 | 0.388137 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 48 | 49999 | 49999 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 80 | 50000 | 50000 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 95 | 50000 | 50000 | 0.000000 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 2 | 50000 | 49996 | 4.636923 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 48 | 50000 | 49999 | 8.972327 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 80 | 50000 | 50000 | 1.729848 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 95 | 50000 | 50000 | 1.729848 | paired |
| b50000-transactional-t8 | cbt-a16 | 2 | 50000 | 48 | 1.698302 | paired |
| b50000-transactional-t8 | cbt-a16 | 48 | 50000 | 26716 | 8.889741 | paired |
| b50000-transactional-t8 | cbt-a16 | 80 | 50000 | 34973 | 1.822278 | paired |
| b50000-transactional-t8 | cbt-a16 | 95 | 50000 | 34973 | 1.822278 | paired |
| b50000-transactional-t8 | cbt-a4 | 2 | 50000 | 48 | 1.698302 | paired |
| b50000-transactional-t8 | cbt-a4 | 48 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a4 | 80 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a4 | 95 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a8 | 2 | 50000 | 48 | 1.698302 | paired |
| b50000-transactional-t8 | cbt-a8 | 48 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a8 | 80 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a8 | 95 | — | — | — | unpaired |

### dem-sierra

| 候选 | 参考 | 机会 | 候选N | 参考N | Dmax px | 状态 |
|---|---|---|---|---|---|---|
| b50000-classic-t1 | b50000-dod-t8 | 2 | 8845 | 8845 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 48 | 12085 | 12085 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 80 | 15919 | 15919 | 0.000000 | paired |
| b50000-classic-t1 | b50000-dod-t8 | 95 | 15919 | 15919 | 0.000000 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 2 | 8874 | 8845 | 1.553716 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 48 | 8960 | 12085 | 1.955863 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 80 | 8976 | 15919 | 1.133568 | paired |
| b50000-transactional-t8 | b50000-dod-t8 | 95 | 8976 | 15919 | 1.133568 | paired |
| b50000-transactional-t8 | cbt-a16 | 2 | 8874 | 48 | 0.951852 | paired |
| b50000-transactional-t8 | cbt-a16 | 48 | 8960 | 22496 | 1.879020 | paired |
| b50000-transactional-t8 | cbt-a16 | 80 | 8976 | 28643 | 1.010911 | paired |
| b50000-transactional-t8 | cbt-a16 | 95 | 8976 | 28643 | 1.010911 | paired |
| b50000-transactional-t8 | cbt-a4 | 2 | 8874 | 48 | 0.951852 | paired |
| b50000-transactional-t8 | cbt-a4 | 48 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a4 | 80 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a4 | 95 | — | — | — | unpaired |
| b50000-transactional-t8 | cbt-a8 | 2 | 8874 | 48 | 0.951852 | paired |
| b50000-transactional-t8 | cbt-a8 | 48 | 8960 | 41522 | 2.015011 | paired |
| b50000-transactional-t8 | cbt-a8 | 80 | 8976 | 56100 | 1.114333 | paired |
| b50000-transactional-t8 | cbt-a8 | 95 | 8976 | 56100 | 1.114333 | paired |


## G. Transactional最近实际N的CBT参考

相同机会按绝对面数差选，平手按case ID；差异百分比以参考N为分母。无效最近点仍列入，不能换成更远有效点。初始48面点保留但不作稳定质量结论。Classic/DOD完整匹配见CSV。

| 候选 | 机会 | N | 最近CBT | 参考N | 数量差% | 候选Emax | 参考Emax | 状态 |
|---|---|---|---|---|---|---|---|---|
| dem-canyon-b50000-transactional-t8 | 2 | 50000 | dem-canyon-cbt-a16 | 48 | 104066.67 | 4.636925 | 63.068155 | ok |
| dem-canyon-b50000-transactional-t8 | 48 | 50000 | dem-canyon-cbt-a8 | 49165 | 1.70 | 9.274477 | — | invalid-quality |
| dem-canyon-b50000-transactional-t8 | 80 | 50000 | dem-canyon-cbt-a16 | 34973 | 42.97 | 2.348193 | 9.069726 | ok |
| dem-canyon-b50000-transactional-t8 | 95 | 50000 | dem-canyon-cbt-a16 | 34973 | 42.97 | 2.348193 | 9.069726 | ok |
| dem-sierra-b50000-transactional-t8 | 2 | 8874 | dem-sierra-cbt-a16 | 48 | 18387.50 | 1.586408 | 16.374504 | ok |
| dem-sierra-b50000-transactional-t8 | 48 | 8960 | dem-sierra-cbt-a16 | 22496 | 60.17 | 2.015012 | 3.189845 | ok |
| dem-sierra-b50000-transactional-t8 | 80 | 8976 | dem-sierra-cbt-a16 | 28643 | 68.66 | 1.133568 | 1.742150 | ok |
| dem-sierra-b50000-transactional-t8 | 95 | 8976 | dem-sierra-cbt-a16 | 28643 | 68.66 | 1.133568 | 1.742150 | ok |
| generated-ridge-b50000-transactional-t8 | 2 | 5467 | generated-ridge-cbt-a16 | 48 | 11289.58 | 1.064190 | 48.631061 | ok |
| generated-ridge-b50000-transactional-t8 | 48 | 5593 | generated-ridge-cbt-a16 | 25662 | 78.21 | 1.163989 | 2.629353 | ok |
| generated-ridge-b50000-transactional-t8 | 80 | 5655 | generated-ridge-cbt-a16 | 24990 | 77.37 | 1.131319 | 2.530013 | ok |
| generated-ridge-b50000-transactional-t8 | 95 | 5747 | generated-ridge-cbt-a16 | 25574 | 77.53 | 1.059956 | 4.148093 | ok |
| peking547-b100000-transactional-t8 | 2 | 99999 | peking547-cbt-a16 | 48 | 208231.25 | 0.330869 | 8.091671 | ok |
| peking547-b100000-transactional-t8 | 15 | 99999 | peking547-cbt-a8 | 80193 | 24.70 | 1.996557 | 1.069818 | ok |
| peking547-b100000-transactional-t8 | 16 | 99999 | peking547-cbt-a4 | 110004 | 9.10 | 0.330869 | 0.653945 | ok |
| peking547-b100000-transactional-t8 | 23 | 99999 | peking547-cbt-a8 | 85238 | 17.32 | 0.330869 | 0.758890 | ok |
| peking547-b200000-transactional-t8 | 2 | 200000 | peking547-cbt-a16 | 48 | 416566.67 | 0.209150 | 8.091671 | ok |
| peking547-b200000-transactional-t8 | 15 | 200000 | peking547-cbt-a4 | 130269 | 53.53 | 1.996557 | 1.069818 | ok |
| peking547-b200000-transactional-t8 | 16 | 200000 | peking547-cbt-a4 | 110004 | 81.81 | 0.209150 | 0.653945 | ok |
| peking547-b200000-transactional-t8 | 23 | 200000 | peking547-cbt-a4 | 140913 | 41.93 | 0.209150 | 0.407000 | ok |
| peking547-b50000-transactional-t8 | 2 | 49999 | peking547-cbt-a16 | 48 | 104064.58 | 0.441886 | 8.091671 | ok |
| peking547-b50000-transactional-t8 | 15 | 49999 | peking547-cbt-a16 | 45112 | 10.83 | 1.996557 | 1.069818 | ok |
| peking547-b50000-transactional-t8 | 16 | 49999 | peking547-cbt-a8 | 63132 | 20.80 | 0.441886 | 0.758890 | ok |
| peking547-b50000-transactional-t8 | 23 | 49999 | peking547-cbt-a16 | 45844 | 9.06 | 0.441886 | 0.927970 | ok |


## H. 科学图与实际画面

原始70组图包含36张科学图、4条路线和30个最坏有效关键帧见证；另附四幅同机会实际画面对照。见证常落到CBT启动帧，不能据此代表其稳定效果，下面额外并列转向与末帧。

Agent已审阅全部图的缩略总览，并单独检查四资产实际对照、质量—成本排版及关键见证。山脊/山地的Transactional可见更明显面片明暗，Emax并不评价法线/着色质量；不能把几何数值验收替代视觉结论。没有据截图认证无裂缝，也未代签用户视觉验收。

### peking547

![peking547 CPU与GPU独立成本轴](figures/fer_02/peking547-runtime.png)

![peking547 同机会实际数量与有效质量](figures/fer_02/peking547-quality-n.png)

![peking547 关键质量与数量轨迹](figures/fer_02/peking547-quality-trajectory.png)

![peking547 逐点差与有效配对覆盖](figures/fer_02/peking547-pointwise-excess.png)

![peking547 真实转向与末帧对照](figures/fer_02/peking547-actual-comparison.png)

其他图：[peking547：阶段成本边界（1）](figures/fer_02/peking547-stages-1.png)；[peking547：阶段成本边界（2）](figures/fer_02/peking547-stages-2.png)；[peking547：阶段成本边界（3）](figures/fer_02/peking547-stages-3.png)；[peking547：阶段成本边界（4）](figures/fer_02/peking547-stages-4.png)；[peking547：阶段成本边界（5）](figures/fer_02/peking547-stages-5.png)；[peking547：固定第一进程的时序与工作量](figures/fer_02/peking547-timeline.png)；[peking547：配置级成本与观测质量](figures/fer_02/peking547-quality-cost.png)。

### generated-ridge

![generated-ridge CPU与GPU独立成本轴](figures/fer_02/generated-ridge-runtime.png)

![generated-ridge 同机会实际数量与有效质量](figures/fer_02/generated-ridge-quality-n.png)

![generated-ridge 关键质量与数量轨迹](figures/fer_02/generated-ridge-quality-trajectory.png)

![generated-ridge 逐点差与有效配对覆盖](figures/fer_02/generated-ridge-pointwise-excess.png)

![generated-ridge 真实转向与末帧对照](figures/fer_02/generated-ridge-actual-comparison.png)

其他图：[generated-ridge：阶段成本边界（1）](figures/fer_02/generated-ridge-stages-1.png)；[generated-ridge：阶段成本边界（2）](figures/fer_02/generated-ridge-stages-2.png)；[generated-ridge：固定第一进程的时序与工作量](figures/fer_02/generated-ridge-timeline.png)；[generated-ridge：配置级成本与观测质量](figures/fer_02/generated-ridge-quality-cost.png)。

### dem-canyon

![dem-canyon CPU与GPU独立成本轴](figures/fer_02/dem-canyon-runtime.png)

![dem-canyon 同机会实际数量与有效质量](figures/fer_02/dem-canyon-quality-n.png)

![dem-canyon 关键质量与数量轨迹](figures/fer_02/dem-canyon-quality-trajectory.png)

![dem-canyon 逐点差与有效配对覆盖](figures/fer_02/dem-canyon-pointwise-excess.png)

![dem-canyon 真实转向与末帧对照](figures/fer_02/dem-canyon-actual-comparison.png)

其他图：[dem-canyon：阶段成本边界（1）](figures/fer_02/dem-canyon-stages-1.png)；[dem-canyon：阶段成本边界（2）](figures/fer_02/dem-canyon-stages-2.png)；[dem-canyon：阶段成本边界（3）](figures/fer_02/dem-canyon-stages-3.png)；[dem-canyon：固定第一进程的时序与工作量](figures/fer_02/dem-canyon-timeline.png)；[dem-canyon：配置级成本与观测质量](figures/fer_02/dem-canyon-quality-cost.png)。

### dem-sierra

![dem-sierra CPU与GPU独立成本轴](figures/fer_02/dem-sierra-runtime.png)

![dem-sierra 同机会实际数量与有效质量](figures/fer_02/dem-sierra-quality-n.png)

![dem-sierra 关键质量与数量轨迹](figures/fer_02/dem-sierra-quality-trajectory.png)

![dem-sierra 逐点差与有效配对覆盖](figures/fer_02/dem-sierra-pointwise-excess.png)

![dem-sierra 真实转向与末帧对照](figures/fer_02/dem-sierra-actual-comparison.png)

其他图：[dem-sierra：阶段成本边界（1）](figures/fer_02/dem-sierra-stages-1.png)；[dem-sierra：阶段成本边界（2）](figures/fer_02/dem-sierra-stages-2.png)；[dem-sierra：固定第一进程的时序与工作量](figures/fer_02/dem-sierra-timeline.png)；[dem-sierra：配置级成本与观测质量](figures/fer_02/dem-sierra-quality-cost.png)。


## I. 可复现产物与约束

[HTML和30个实际帧播放器](../../../benchmark-output/experiment-infrastructure/fer-02/report/index.html)保留全部图及相对原始链接。PNG/SVG/PDF/FigureSpec来自同一analysis；Git提交PNG与精简表，mesh/程序/完整误差数组保留在忽略目录。

[进程表](data/fer_02/processes.csv)、[质量表](data/fer_02/quality.csv)、[逐点表](data/fer_02/pointwise.csv)、[最近数量](data/fer_02/numberMatches.csv)、[所有分组指标](data/fer_02/group-metrics.csv)、[GPU来源代指标](data/fer_02/gpu-metrics.csv)、[图片来源清单](data/fer_02/figure-manifest.json)。

本页没有新增自然样本、改阈值或修复算法。无效原因、跨视图恢复与旧CBI工程回归继续开放；下一轮修改应使用新协议和新输出根，不覆盖FER-02。
