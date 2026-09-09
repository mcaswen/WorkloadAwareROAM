"""对同一冻结输入的完整块计算探索性差值，不执行正式显著性推断。"""

import math
import statistics

NOISE_RULE_VERSION = 1


def finite_nonnegative(value, name):
    value = float(value)
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"{name} 必须是有限非负数")
    return value


def percentile(values, quantile):
    """按相邻次序统计量线性插值，空集合与非法分位直接拒绝。"""
    ordered = sorted(finite_nonnegative(value, "耗时") for value in values)
    if not ordered or not math.isfinite(quantile) or not 0 <= quantile <= 1:
        raise ValueError("百分位需要非空数据和 0..1 分位")
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize_pair(times_a, times_b, calibration_ms):
    """先逐块求 A-B，再取中位数；重复块不能充当独立目标或独立运行。"""
    a = [finite_nonnegative(value, "A 耗时") for value in times_a]
    b = [finite_nonnegative(value, "B 耗时") for value in times_b]
    if not a or len(a) != len(b):
        raise ValueError("配对必须包含相同数量的非空完整块")
    calibration_ms = finite_nonnegative(calibration_ms, "标定噪声")
    differences = [left - right for left, right in zip(a, b)]
    median_a = statistics.median(a)
    median_b = statistics.median(b)
    median_difference = statistics.median(differences)
    mad = statistics.median(abs(value - median_difference) for value in differences)
    threshold = max(.01, .03 * min(median_a, median_b), calibration_ms, 3 * mad)
    winner = "A" if median_difference < -threshold else ("B" if median_difference > threshold else "tie")
    return {
        "pairCount": len(a),
        "medianAMs": median_a,
        "medianBMs": median_b,
        "p95AMs": percentile(a, .95),
        "p95BMs": percentile(b, .95),
        "medianDifferenceMs": median_difference,
        "differenceMadMs": mad,
        "calibrationNoiseMs": calibration_ms,
        "thresholdMs": threshold,
        "relativeGainB": median_difference / median_a if median_a > 0 else None,
        "relativeDenominator": "medianAMs",
        "winner": winner,
        "interpretation": {"A": "A 占优", "B": "B 占优", "tie": "平局或噪声不足以判断"}[winner],
        "noiseRuleVersion": NOISE_RULE_VERSION,
    }


def run_drift(medians):
    """运行级极差超过中位数的 5% 即不稳定，近零值同时保留绝对量。"""
    values = [finite_nonnegative(value, "运行中位数") for value in medians]
    if len(values) != 3:
        raise ValueError("短运行漂移核验需要三个独立运行")
    center = statistics.median(values)
    spread = max(values) - min(values)
    return {"medianMs": center, "rangeMs": spread, "relativeRange": spread / center if center else None,
            "stable": spread <= .05 * center, "nearZero": center <= .01}

