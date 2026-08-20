from __future__ import annotations

import csv
import math
from collections import defaultdict
from pathlib import Path


ROOT = Path("benchmark-output/report-experiments")

COLORS = {
    "Classic CPU ROAM": "#4C78A8",
    "Data-Oriented CPU ROAM": "#F58518",
    "CPU update": "#4C78A8",
    "CPU upload": "#F58518",
    "Other LOD": "#9D755D",
    "Split": "#4C78A8",
    "Merge": "#F58518",
    "Emit": "#54A24B",
    "Validate": "#E45756",
    "Other stage": "#9D755D",
    "Avg": "#4C78A8",
    "P50": "#F58518",
    "P90": "#54A24B",
    "P95": "#E45756",
}

ALGORITHM_LABELS = {
    "Classic CPU ROAM": "经典 CPU ROAM",
    "Data-Oriented CPU ROAM": "DOD CPU ROAM",
}

CASE_LABELS = {
    "baseline": "默认参数",
    "depth 12": "最大深度 12",
    "depth 14": "最大深度 14",
    "depth 16": "最大深度 16",
    "depth 18": "最大深度 18",
    "depth 20": "最大深度 20",
    "distance 20": "距离权重 20",
    "distance 40": "距离权重 40",
    "distance 60": "距离权重 60",
    "distance 80": "距离权重 80",
    "Test129": "Test129 高度图",
    "Peking513": "Peking513 高度图",
}

COMPONENT_LABELS = {
    "Snapshot": "快照构建",
    "Buffer alloc": "缓冲区分配",
    "CPU update": "CPU 更新",
    "CPU upload": "CPU 上传",
    "Other LOD": "其他 LOD",
    "Split": "分裂",
    "Merge": "合并",
    "Emit": "网格输出",
    "Validate": "拓扑验证",
    "Other stage": "其他阶段",
    "LOD total": "LOD 总耗时",
    "ms / 10k triangles": "每万三角形耗时",
    "Avg": "平均值",
    "P50": "P50",
    "P90": "P90",
    "P95": "P95",
}

NUMERIC_COLUMNS = [
    "timeSeconds",
    "frameMilliseconds",
    "triangles",
    "nodes",
    "cpuUtilizationPercent",
    "lodTotalMilliseconds",
    "cpuUpdateMilliseconds",
    "cpuUploadMilliseconds",
    "splitMilliseconds",
    "mergeMilliseconds",
    "emitMilliseconds",
    "validateMilliseconds",
    "maxDepthReached",
    "maxDepthSetting",
    "distanceScale",
]


def esc(value: object) -> str:
    text = str(value)
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def display_label(value: str) -> str:
    return ALGORITHM_LABELS.get(value, CASE_LABELS.get(value, COMPONENT_LABELS.get(value, value)))


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    k = (len(ordered) - 1) * percent / 100.0
    low = math.floor(k)
    high = math.ceil(k)
    if low == high:
        return ordered[low]
    return ordered[low] * (high - k) + ordered[high] * (k - low)


def read_rows(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            for key in NUMERIC_COLUMNS:
                if key in row and row[key] != "":
                    row[key] = float(row[key])
            row["_source"] = path.name
            rows.append(row)
    return rows


def read_experiment(folder: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in sorted(folder.glob("exp*.csv")):
        rows.extend(read_rows(path))
    return rows


def group_by(rows: list[dict[str, object]], key: str) -> dict[object, list[dict[str, object]]]:
    grouped: dict[object, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[row[key]].append(row)
    return grouped


def metric_values(
    rows: list[dict[str, object]],
    key: str,
    fallback_key: str | None = None,
) -> list[float]:
    values: list[float] = []
    for row in rows:
        if key in row and row[key] != "":
            values.append(float(row[key]))
        elif fallback_key is not None and fallback_key in row and row[fallback_key] != "":
            values.append(float(row[fallback_key]))
        else:
            values.append(0.0)
    return values


def row_metric(row: dict[str, object], key: str, fallback_key: str | None = None) -> float:
    if key in row and row[key] != "":
        return float(row[key])
    if fallback_key is not None and fallback_key in row and row[fallback_key] != "":
        return float(row[fallback_key])
    return 0.0


def stats_for(rows: list[dict[str, object]]) -> dict[str, float]:
    lod = [float(row["lodTotalMilliseconds"]) for row in rows]
    frame = [float(row["frameMilliseconds"]) for row in rows]
    triangles = [float(row["triangles"]) for row in rows]
    cpu_update = [float(row["cpuUpdateMilliseconds"]) for row in rows]
    upload = [float(row["cpuUploadMilliseconds"]) for row in rows]
    split = [float(row["splitMilliseconds"]) for row in rows]
    merge = [float(row["mergeMilliseconds"]) for row in rows]
    emit = [float(row["emitMilliseconds"]) for row in rows]
    validate = [float(row["validateMilliseconds"]) for row in rows]
    cpu_percent = [float(row["cpuUtilizationPercent"]) for row in rows]
    depth_reached = [float(row["maxDepthReached"]) for row in rows]

    avg_lod = mean(lod)
    avg_triangles = mean(triangles)
    components = {
        "cpuUpdateMs": mean(cpu_update),
        "cpuUploadMs": mean(upload),
    }
    stage_components = {
        "splitMs": mean(split),
        "mergeMs": mean(merge),
        "emitMs": mean(emit),
        "validateMs": mean(validate),
    }
    known = sum(components.values())
    stage_known = sum(stage_components.values())
    return {
        "samples": float(len(rows)),
        "avgFrameMs": mean(frame),
        "p50FrameMs": percentile(frame, 50),
        "p90FrameMs": percentile(frame, 90),
        "p95FrameMs": percentile(frame, 95),
        "avgLodMs": avg_lod,
        "p50LodMs": percentile(lod, 50),
        "p90LodMs": percentile(lod, 90),
        "p95LodMs": percentile(lod, 95),
        "avgTriangles": avg_triangles,
        "avgNodes": mean([float(row["nodes"]) for row in rows]),
        "avgCpuPercent": mean(cpu_percent),
        "maxDepthReached": max(depth_reached) if depth_reached else 0.0,
        "lodMsPer10kTriangles": (avg_lod / avg_triangles * 10000.0) if avg_triangles else 0.0,
        "otherLodMs": max(avg_lod - known, 0.0),
        "otherStageMs": max(avg_lod - stage_known, 0.0),
        **components,
        **stage_components,
    }


def svg_header(width: int, height: int) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,"Microsoft YaHei",sans-serif;fill:#222}.small{font-size:12px}.axis{stroke:#333;stroke-width:1}.grid{stroke:#E5E5E5;stroke-width:1}</style>',
    ]


def write_svg(path: Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def nice_max(value: float) -> float:
    if value <= 0:
        return 1.0
    scale = 10 ** math.floor(math.log10(value))
    normalized = value / scale
    if normalized <= 1.5:
        return 1.5 * scale
    if normalized <= 3.0:
        return 3.0 * scale
    if normalized <= 6.0:
        return 6.0 * scale
    return 10.0 * scale


def draw_axes(lines: list[str], left: int, top: int, right: int, bottom: int, max_y: float, y_label: str) -> None:
    chart_h = bottom - top
    for i in range(6):
        y = bottom - chart_h * i / 5
        value = max_y * i / 5
        lines.append(f'<line class="grid" x1="{left}" y1="{y:.1f}" x2="{right}" y2="{y:.1f}"/>')
        lines.append(f'<text class="small" x="{left - 10}" y="{y + 4:.1f}" text-anchor="end">{value:.2f}</text>')
    lines.append(f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{bottom}"/>')
    lines.append(f'<line class="axis" x1="{left}" y1="{bottom}" x2="{right}" y2="{bottom}"/>')
    lines.append(
        f'<text x="24" y="{(top + bottom) / 2:.1f}" font-size="14" text-anchor="middle" transform="rotate(-90 24 {(top + bottom) / 2:.1f})">{esc(y_label)}</text>'
    )


def legend(lines: list[str], items: list[tuple[str, str]], x: int, y: int, width: int) -> None:
    step = max(150, width // max(1, len(items)))
    for idx, (label, color) in enumerate(items):
        lx = x + idx * step
        lines.append(f'<rect x="{lx}" y="{y}" width="14" height="14" fill="{color}" rx="2"/>')
        lines.append(f'<text class="small" x="{lx + 20}" y="{y + 12}" text-anchor="start">{esc(display_label(label))}</text>')


def bar_chart(
    path: Path,
    title: str,
    categories: list[str],
    series: list[tuple[str, list[float], str]],
    y_label: str,
    value_format: str = "{:.2f}",
) -> None:
    width, height = 1080, 640
    left, right, top, bottom = 92, 1040, 78, 510
    max_y = nice_max(max([max(values) for _, values, _ in series] + [1.0]) * 1.08)
    lines = svg_header(width, height)
    lines.append(f'<text x="{width / 2}" y="34" font-size="22" text-anchor="middle">{esc(title)}</text>')
    draw_axes(lines, left, top, right, bottom, max_y, y_label)
    group_w = (right - left) / len(categories)
    bar_w = min(42, group_w / (len(series) + 1.2))
    for ci, category in enumerate(categories):
        center = left + group_w * (ci + 0.5)
        start = center - bar_w * len(series) / 2
        for si, (label, values, color) in enumerate(series):
            value = values[ci]
            h = (bottom - top) * value / max_y
            x = start + si * bar_w
            y = bottom - h
            lines.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w * 0.85:.1f}" height="{h:.1f}" fill="{color}" rx="2"/>')
            lines.append(f'<text class="small" x="{x + bar_w * 0.425:.1f}" y="{y - 6:.1f}" text-anchor="middle">{esc(value_format.format(value))}</text>')
        lines.append(f'<text class="small" x="{center:.1f}" y="{bottom + 36}" text-anchor="middle">{esc(display_label(category))}</text>')
    legend(lines, [(label, color) for label, _, color in series], left, 580, right - left)
    lines.append("</svg>")
    write_svg(path, lines)


def line_chart(
    path: Path,
    title: str,
    x_labels: list[str],
    series: list[tuple[str, list[float], str]],
    y_label: str,
    value_format: str = "{:.1f}",
) -> None:
    width, height = 1080, 640
    left, right, top, bottom = 92, 1040, 78, 510
    max_y = nice_max(max([max(values) for _, values, _ in series] + [1.0]) * 1.08)
    lines = svg_header(width, height)
    lines.append(f'<text x="{width / 2}" y="34" font-size="22" text-anchor="middle">{esc(title)}</text>')
    draw_axes(lines, left, top, right, bottom, max_y, y_label)
    span = right - left
    chart_h = bottom - top
    denom = max(1, len(x_labels) - 1)
    for xi, label in enumerate(x_labels):
        x = left + span * xi / denom
        lines.append(f'<line class="grid" x1="{x:.1f}" y1="{bottom}" x2="{x:.1f}" y2="{bottom + 5}"/>')
        lines.append(f'<text class="small" x="{x:.1f}" y="{bottom + 34}" text-anchor="middle">{esc(label)}</text>')
    for label, values, color in series:
        points = []
        for xi, value in enumerate(values):
            x = left + span * xi / denom
            y = bottom - chart_h * value / max_y
            points.append(f"{x:.1f},{y:.1f}")
        lines.append(f'<polyline points="{" ".join(points)}" fill="none" stroke="{color}" stroke-width="3"/>')
        for xi, value in enumerate(values):
            x = left + span * xi / denom
            y = bottom - chart_h * value / max_y
            lines.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>')
            lines.append(f'<text class="small" x="{x:.1f}" y="{y - 8:.1f}" text-anchor="middle">{esc(value_format.format(value))}</text>')
    legend(lines, [(label, color) for label, _, color in series], left, 580, right - left)
    lines.append("</svg>")
    write_svg(path, lines)


def xy_line_chart(
    path: Path,
    title: str,
    series: list[tuple[str, list[tuple[float, float]], str]],
    y_label: str,
) -> None:
    width, height = 1080, 640
    left, right, top, bottom = 92, 1040, 78, 510
    all_x = [x for _, points, _ in series for x, _ in points]
    all_y = [y for _, points, _ in series for _, y in points]
    min_x, max_x = min(all_x), max(all_x)
    max_y = nice_max(max(all_y) * 1.08)
    lines = svg_header(width, height)
    lines.append(f'<text x="{width / 2}" y="34" font-size="22" text-anchor="middle">{esc(title)}</text>')
    draw_axes(lines, left, top, right, bottom, max_y, y_label)
    for i in range(6):
        x = left + (right - left) * i / 5
        value = min_x + (max_x - min_x) * i / 5
        lines.append(f'<text class="small" x="{x:.1f}" y="{bottom + 34}" text-anchor="middle">{value:.1f}s</text>')
    for label, points, color in series:
        coords = []
        for x_value, y_value in points:
            x = left + (right - left) * (x_value - min_x) / max(max_x - min_x, 0.0001)
            y = bottom - (bottom - top) * y_value / max_y
            coords.append(f"{x:.1f},{y:.1f}")
        lines.append(f'<polyline points="{" ".join(coords)}" fill="none" stroke="{color}" stroke-width="2.5"/>')
    legend(lines, [(label, color) for label, _, color in series], left, 580, right - left)
    lines.append("</svg>")
    write_svg(path, lines)


def stacked_bar_chart(
    path: Path,
    title: str,
    categories: list[str],
    components: list[tuple[str, list[float], str]],
    y_label: str,
) -> None:
    width, height = 1080, 660
    left, right, top, bottom = 92, 1040, 78, 520
    totals = [sum(values[i] for _, values, _ in components) for i in range(len(categories))]
    max_y = nice_max(max(totals + [1.0]) * 1.08)
    lines = svg_header(width, height)
    lines.append(f'<text x="{width / 2}" y="34" font-size="22" text-anchor="middle">{esc(title)}</text>')
    draw_axes(lines, left, top, right, bottom, max_y, y_label)
    group_w = (right - left) / len(categories)
    bar_w = min(94, group_w * 0.42)
    for ci, category in enumerate(categories):
        x = left + group_w * (ci + 0.5) - bar_w / 2
        current = bottom
        for label, values, color in components:
            value = values[ci]
            h = (bottom - top) * value / max_y
            current -= h
            lines.append(f'<rect x="{x:.1f}" y="{current:.1f}" width="{bar_w:.1f}" height="{h:.1f}" fill="{color}" rx="1"/>')
        lines.append(f'<text class="small" x="{x + bar_w / 2:.1f}" y="{current - 8:.1f}" text-anchor="middle">{totals[ci]:.2f}</text>')
        lines.append(f'<text class="small" x="{x + bar_w / 2:.1f}" y="{bottom + 36}" text-anchor="middle">{esc(display_label(category))}</text>')
    legend(lines, [(label, color) for label, _, color in components], left, 600, right - left)
    lines.append("</svg>")
    write_svg(path, lines)


def donut_chart(path: Path, title: str, values: list[tuple[str, float, str]]) -> None:
    width, height = 900, 620
    cx, cy, radius, inner = 330, 320, 190, 105
    total = sum(value for _, value, _ in values)
    lines = svg_header(width, height)
    lines.append(f'<text x="{width / 2}" y="34" font-size="22" text-anchor="middle">{esc(title)}</text>')
    start = -math.pi / 2
    for label, value, color in values:
        angle = 2 * math.pi * value / total if total else 0.0
        end = start + angle
        large = 1 if angle > math.pi else 0
        x1, y1 = cx + radius * math.cos(start), cy + radius * math.sin(start)
        x2, y2 = cx + radius * math.cos(end), cy + radius * math.sin(end)
        ix2, iy2 = cx + inner * math.cos(end), cy + inner * math.sin(end)
        ix1, iy1 = cx + inner * math.cos(start), cy + inner * math.sin(start)
        lines.append(
            f'<path d="M{x1:.1f},{y1:.1f} A{radius},{radius} 0 {large} 1 {x2:.1f},{y2:.1f} L{ix2:.1f},{iy2:.1f} A{inner},{inner} 0 {large} 0 {ix1:.1f},{iy1:.1f} Z" fill="{color}"/>'
        )
        mid = (start + end) / 2
        tx, ty = cx + (radius + 28) * math.cos(mid), cy + (radius + 28) * math.sin(mid)
        percent = value / total * 100 if total else 0.0
        if percent >= 4.0:
            lines.append(f'<text class="small" x="{tx:.1f}" y="{ty:.1f}" text-anchor="middle">{percent:.1f}%</text>')
        start = end
    lines.append(f'<text x="{cx}" y="{cy - 4}" font-size="18" text-anchor="middle">LOD 总耗时</text>')
    lines.append(f'<text x="{cx}" y="{cy + 24}" font-size="18" text-anchor="middle">{total:.2f} ms</text>')
    for idx, (label, value, color) in enumerate(values):
        y = 150 + idx * 42
        percent = value / total * 100 if total else 0.0
        lines.append(f'<rect x="610" y="{y - 14}" width="16" height="16" fill="{color}" rx="2"/>')
        lines.append(f'<text class="small" x="636" y="{y}" text-anchor="start">{esc(display_label(label))}: {value:.2f} ms / {percent:.1f}%</text>')
    lines.append("</svg>")
    write_svg(path, lines)


def write_analysis_csv(folder: Path, rows: list[dict[str, object]]) -> dict[tuple[str, str], dict[str, float]]:
    by_case_alg: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        source = str(row["_source"])
        if folder.name == "experiment-01-baseline":
            case = "baseline"
        elif folder.name == "experiment-02-max-depth":
            case = f"depth {int(float(row['maxDepthSetting']))}"
        elif folder.name == "experiment-03-distance-scale":
            case = f"distance {int(float(row['distanceScale']))}"
        elif folder.name == "experiment-04-heightmap":
            case = "Peking513" if "peking" in source.lower() else "Test129"
        else:
            case = "baseline"
        by_case_alg[(case, str(row["algorithm"]))].append(row)

    stats: dict[tuple[str, str], dict[str, float]] = {}
    metric_names = [
        "samples",
        "avgFrameMs",
        "p50FrameMs",
        "p90FrameMs",
        "p95FrameMs",
        "avgLodMs",
        "p50LodMs",
        "p90LodMs",
        "p95LodMs",
        "avgTriangles",
        "avgNodes",
        "avgCpuPercent",
        "maxDepthReached",
        "lodMsPer10kTriangles",
        "cpuUpdateMs",
        "cpuUploadMs",
        "otherLodMs",
        "splitMs",
        "mergeMs",
        "emitMs",
        "validateMs",
        "otherStageMs",
    ]
    for key, group in sorted(by_case_alg.items()):
        stats[key] = stats_for(group)
    with (folder / "analysis_metrics.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["case", "algorithm", *metric_names])
        for (case, algorithm), values in sorted(stats.items()):
            writer.writerow([case, algorithm, *[f"{values[name]:.6f}" for name in metric_names]])
    return stats


def stats_by_algorithm(stats: dict[tuple[str, str], dict[str, float]], case: str) -> dict[str, dict[str, float]]:
    return {algorithm: values for (case_name, algorithm), values in stats.items() if case_name == case}


def cases_for_folder(folder_name: str) -> list[str]:
    if folder_name == "experiment-01-baseline":
        return ["baseline"]
    if folder_name == "experiment-02-max-depth":
        return [f"depth {depth}" for depth in [12, 14, 16, 18, 20]]
    if folder_name == "experiment-03-distance-scale":
        return [f"distance {distance}" for distance in [20, 40, 60, 80]]
    if folder_name == "experiment-04-heightmap":
        return ["Test129", "Peking513"]
    return ["baseline"]


def generate_common_charts(folder: Path, stats: dict[tuple[str, str], dict[str, float]]) -> None:
    algorithms = ["Classic CPU ROAM", "Data-Oriented CPU ROAM"]
    cases = cases_for_folder(folder.name)
    cases = [case for case in cases if all((case, algorithm) in stats for algorithm in algorithms)]

    bar_chart(
        folder / "chart_avg_lod_ms.svg",
        "平均 LOD 耗时对比",
        cases,
        [(a, [stats[(case, a)]["avgLodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "平均 LOD 耗时（毫秒）",
    )
    bar_chart(
        folder / "chart_avg_frame_ms.svg",
        "平均帧耗时对比",
        cases,
        [(a, [stats[(case, a)]["avgFrameMs"] for case in cases], COLORS[a]) for a in algorithms],
        "平均帧耗时（毫秒）",
    )
    bar_chart(
        folder / "chart_avg_triangles.svg",
        "平均生成三角形数对比",
        cases,
        [(a, [stats[(case, a)]["avgTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "三角形数",
        "{:.0f}",
    )
    bar_chart(
        folder / "chart_avg_cpu_percent.svg",
        "平均进程 CPU 利用率对比",
        cases,
        [(a, [stats[(case, a)]["avgCpuPercent"] for case in cases], COLORS[a]) for a in algorithms],
        "CPU 利用率（%）",
    )

def roam_stage_components(data: dict[str, dict[str, float]], algorithms: list[str]) -> list[tuple[str, list[float], str]]:
    return [
        ("Split", [data[a]["splitMs"] for a in algorithms], COLORS["Split"]),
        ("Merge", [data[a]["mergeMs"] for a in algorithms], COLORS["Merge"]),
        ("Emit", [data[a]["emitMs"] for a in algorithms], COLORS["Emit"]),
        ("Validate", [data[a]["validateMs"] for a in algorithms], COLORS["Validate"]),
        ("Other stage", [data[a]["otherStageMs"] for a in algorithms], COLORS["Other stage"]),
    ]


def generate_experiment_01(folder: Path, stats: dict[tuple[str, str], dict[str, float]]) -> None:
    data = stats_by_algorithm(stats, "baseline")
    algorithms = ["Classic CPU ROAM", "Data-Oriented CPU ROAM"]
    percentile_fields = [("平均值", "avgLodMs"), ("P50", "p50LodMs"), ("P90", "p90LodMs"), ("P95", "p95LodMs")]
    bar_chart(
        folder / "chart_exp01_lod_percentiles.svg",
        "实验1：LOD 耗时分布",
        [label for label, _ in percentile_fields],
        [(algorithm, [data[algorithm][field] for _, field in percentile_fields], COLORS[algorithm]) for algorithm in algorithms],
        "毫秒",
    )
    bar_chart(
        folder / "chart_exp01_lod_efficiency.svg",
        "实验1：单位三角形规模的 LOD 耗时",
        algorithms,
        [("ms / 10k triangles", [data[algorithm]["lodMsPer10kTriangles"] for algorithm in algorithms], "#4C78A8")],
        "毫秒 / 1 万三角形",
    )
    components = [
        ("CPU update", [data[a]["cpuUpdateMs"] for a in algorithms], COLORS["CPU update"]),
        ("CPU upload", [data[a]["cpuUploadMs"] for a in algorithms], COLORS["CPU upload"]),
        ("Other LOD", [data[a]["otherLodMs"] for a in algorithms], COLORS["Other LOD"]),
    ]
    stacked_bar_chart(folder / "chart_exp01_lod_composition.svg", "实验1：LOD 时间组成", algorithms, components, "毫秒")
    stacked_bar_chart(
        folder / "chart_exp01_roam_stage_composition.svg",
        "实验1：ROAM 阶段耗时组成",
        algorithms,
        roam_stage_components(data, algorithms),
        "毫秒",
    )


def generate_experiment_02(folder: Path, stats: dict[tuple[str, str], dict[str, float]]) -> None:
    algorithms = ["Classic CPU ROAM", "Data-Oriented CPU ROAM"]
    cases = [f"depth {depth}" for depth in [12, 14, 16, 18, 20]]
    x_labels = ["12", "14", "16", "18", "20"]
    line_chart(
        folder / "chart_exp02_depth_lod_lines.svg",
        "实验2：最大深度对平均 LOD 耗时的影响",
        x_labels,
        [(a, [stats[(case, a)]["avgLodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "平均 LOD 耗时（毫秒）",
    )
    line_chart(
        folder / "chart_exp02_depth_p95_lod_lines.svg",
        "实验2：最大深度对 P95 LOD 耗时的影响",
        x_labels,
        [(a, [stats[(case, a)]["p95LodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "P95 LOD 耗时（毫秒）",
    )
    line_chart(
        folder / "chart_exp02_depth_triangles_lines.svg",
        "实验2：最大深度对平均三角形数的影响",
        x_labels,
        [(a, [stats[(case, a)]["avgTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "三角形数",
        "{:.0f}",
    )
    line_chart(
        folder / "chart_exp02_depth_efficiency.svg",
        "实验2：最大深度下的单位三角形 LOD 耗时",
        x_labels,
        [(a, [stats[(case, a)]["lodMsPer10kTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "毫秒 / 1 万三角形",
    )


def generate_experiment_03(folder: Path, stats: dict[tuple[str, str], dict[str, float]]) -> None:
    algorithms = ["Classic CPU ROAM", "Data-Oriented CPU ROAM"]
    cases = [f"distance {distance}" for distance in [20, 40, 60, 80]]
    x_labels = ["20", "40", "60", "80"]
    line_chart(
        folder / "chart_exp03_distance_lod_lines.svg",
        "实验3：距离权重对平均 LOD 耗时的影响",
        x_labels,
        [(a, [stats[(case, a)]["avgLodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "平均 LOD 耗时（毫秒）",
    )
    line_chart(
        folder / "chart_exp03_distance_p95_lod_lines.svg",
        "实验3：距离权重对 P95 LOD 耗时的影响",
        x_labels,
        [(a, [stats[(case, a)]["p95LodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "P95 LOD 耗时（毫秒）",
    )
    line_chart(
        folder / "chart_exp03_distance_triangles_lines.svg",
        "实验3：距离权重对平均三角形数的影响",
        x_labels,
        [(a, [stats[(case, a)]["avgTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "三角形数",
        "{:.0f}",
    )
    line_chart(
        folder / "chart_exp03_distance_efficiency.svg",
        "实验3：距离权重下的单位三角形 LOD 耗时",
        x_labels,
        [(a, [stats[(case, a)]["lodMsPer10kTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "毫秒 / 1 万三角形",
    )


def generate_experiment_04(folder: Path, stats: dict[tuple[str, str], dict[str, float]]) -> None:
    algorithms = ["Classic CPU ROAM", "Data-Oriented CPU ROAM"]
    cases = ["Test129", "Peking513"]
    bar_chart(
        folder / "chart_exp04_heightmap_lod_percentiles.svg",
        "实验4：不同高度图的平均 LOD 耗时",
        cases,
        [(a, [stats[(case, a)]["avgLodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "平均 LOD 耗时（毫秒）",
    )
    bar_chart(
        folder / "chart_exp04_heightmap_p95_lod.svg",
        "实验4：不同高度图的 P95 LOD 耗时",
        cases,
        [(a, [stats[(case, a)]["p95LodMs"] for case in cases], COLORS[a]) for a in algorithms],
        "P95 LOD 耗时（毫秒）",
    )
    bar_chart(
        folder / "chart_exp04_heightmap_triangles.svg",
        "实验4：不同高度图的平均三角形数",
        cases,
        [(a, [stats[(case, a)]["avgTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "三角形数",
        "{:.0f}",
    )
    bar_chart(
        folder / "chart_exp04_heightmap_efficiency.svg",
        "实验4：不同高度图下的单位三角形 LOD 耗时",
        cases,
        [(a, [stats[(case, a)]["lodMsPer10kTriangles"] for case in cases], COLORS[a]) for a in algorithms],
        "毫秒 / 1 万三角形",
    )


def generate_experiment_05(folder: Path, stats: dict[tuple[str, str], dict[str, float]], rows: list[dict[str, object]]) -> None:
    data = stats_by_algorithm(stats, "baseline")
    algorithms = ["Classic CPU ROAM", "Data-Oriented CPU ROAM"]
    percentile_fields = [("平均值", "avgLodMs"), ("P50", "p50LodMs"), ("P90", "p90LodMs"), ("P95", "p95LodMs")]
    bar_chart(
        folder / "chart_exp05_lod_percentiles.svg",
        "实验5：LOD 耗时分布",
        [label for label, _ in percentile_fields],
        [(algorithm, [data[algorithm][field] for _, field in percentile_fields], COLORS[algorithm]) for algorithm in algorithms],
        "毫秒",
    )
    focus = data["Data-Oriented CPU ROAM"]
    donut_values = [
        ("CPU update", focus["cpuUpdateMs"], COLORS["CPU update"]),
        ("CPU upload", focus["cpuUploadMs"], COLORS["CPU upload"]),
        ("Split", focus["splitMs"], COLORS["Split"]),
        ("Merge", focus["mergeMs"], COLORS["Merge"]),
        ("Emit", focus["emitMs"], COLORS["Emit"]),
        ("Validate", focus["validateMs"], COLORS["Validate"]),
        ("Other LOD", focus["otherLodMs"], COLORS["Other LOD"]),
    ]
    donut_chart(folder / "chart_exp05_cpu_pipeline_share.svg", "实验5：DOD CPU 的 LOD 时间占比", donut_values)

    focus_rows = [row for row in rows if row["algorithm"] == "Data-Oriented CPU ROAM"]
    step = max(1, len(focus_rows) // 260)
    sampled = focus_rows[::step]
    xy_line_chart(
        folder / "chart_exp05_cpu_timing_over_time.svg",
        "实验5：DOD CPU 沿相机路径的耗时变化",
        [
            ("LOD total", [(float(row["timeSeconds"]), float(row["lodTotalMilliseconds"])) for row in sampled], "#333333"),
            ("CPU update", [(float(row["timeSeconds"]), float(row["cpuUpdateMilliseconds"])) for row in sampled], COLORS["CPU update"]),
            ("CPU upload", [(float(row["timeSeconds"]), float(row["cpuUploadMilliseconds"])) for row in sampled], COLORS["CPU upload"]),
        ],
        "毫秒",
    )
    xy_line_chart(
        folder / "chart_exp05_roam_stage_timing_over_time.svg",
        "实验5：DOD CPU ROAM 阶段耗时变化",
        [
            ("Split", [(float(row["timeSeconds"]), float(row["splitMilliseconds"])) for row in sampled], COLORS["Split"]),
            ("Merge", [(float(row["timeSeconds"]), float(row["mergeMilliseconds"])) for row in sampled], COLORS["Merge"]),
            ("Emit", [(float(row["timeSeconds"]), float(row["emitMilliseconds"])) for row in sampled], COLORS["Emit"]),
            ("Validate", [(float(row["timeSeconds"]), float(row["validateMilliseconds"])) for row in sampled], COLORS["Validate"]),
        ],
        "毫秒",
    )


def main() -> None:
    if not ROOT.exists():
        raise SystemExit(f"Missing {ROOT}")
    for folder in sorted(ROOT.glob("experiment-*")):
        rows = read_experiment(folder)
        if not rows:
            continue
        stats = write_analysis_csv(folder, rows)
        generate_common_charts(folder, stats)
        if folder.name == "experiment-01-baseline":
            generate_experiment_01(folder, stats)
        elif folder.name == "experiment-02-max-depth":
            generate_experiment_02(folder, stats)
        elif folder.name == "experiment-03-distance-scale":
            generate_experiment_03(folder, stats)
        elif folder.name == "experiment-04-heightmap":
            generate_experiment_04(folder, stats)
        elif folder.name == "experiment-05-gpu-breakdown":
            generate_experiment_05(folder, stats, rows)
        print(f"generated charts for {folder.name}")


if __name__ == "__main__":
    main()
