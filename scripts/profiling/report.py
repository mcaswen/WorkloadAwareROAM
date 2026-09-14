"""函数采样和窗口的可审计聚合；采样权重不是精确函数计时。"""

from bisect import bisect_right
from collections import Counter
import csv
import io
import re


def read_windows(text, expected=None):
    if not text.endswith("# complete\n"):
        raise ValueError("采集窗口没有完整结束标志")
    rows = list(csv.DictReader(io.StringIO(text.removesuffix("# complete\n"))))
    required = {"replay", "round", "start_ns", "end_ns", "enable_ns", "disable_ns"}
    windows = []
    for row in rows:
        if set(row) != required or any(v is None for v in row.values()):
            raise ValueError("窗口字段不完整")
        value = {k: int(v) for k, v in row.items()}
        if value["end_ns"] <= value["start_ns"] or min(value.values()) < 0:
            raise ValueError("窗口时钟或元数据非法")
        if windows and value["start_ns"] < windows[-1]["end_ns"]:
            raise ValueError("窗口存在重叠或逆序")
        windows.append(value)
    if not windows or (expected is not None and len(windows) != expected):
        raise ValueError("窗口数量与冻结轨迹不符")
    if len({(w["replay"], w["round"]) for w in windows}) != len(windows):
        raise ValueError("重复窗口身份")
    return windows


def samples(text):
    header = re.compile(r"^.*?\b(\d+)/(\d+)\s+(\d+)\.(\d+):\s+(\d+)\s+cpu-clock:u:")
    for block in text.strip().split("\n\n"):
        lines = block.splitlines()
        if not lines:
            continue
        match = header.match(lines[0])
        if not match:
            raise ValueError(f"未知 perf 样本头：{lines[0][:160]}")
        frames = []
        for line in lines[1:]:
            frame = re.match(r"\s+[0-9a-f]+\s+(.+)$", line)
            if frame:
                value = frame[1]
                if value.endswith(" (inlined)"):
                    value = value.removesuffix(" (inlined)")
                elif " (" in value:
                    value = value.rsplit(" (", 1)[0]
                frames.append(value)
        yield {"pid": int(match[1]), "tid": int(match[2]),
               "time_ns": int(match[3]) * 1000000000 + int(match[4].ljust(9, "0")),
               "period": int(match[5]), "frames": frames}


def summarize_perf(text, windows):
    starts = [w["start_ns"] for w in windows]
    own, inclusive, paths = Counter(), Counter(), Counter()
    own_samples, thread_samples, frame_samples = Counter(), Counter(), Counter()
    total = inside = weight = missing = unknown = shallow = outside = unknown_ancestor = 0
    for sample in samples(text):
        total += 1
        index = bisect_right(starts, sample["time_ns"]) - 1
        if index < 0 or sample["time_ns"] >= windows[index]["end_ns"]:
            outside += 1
            continue
        inside += 1
        period = sample["period"]
        weight += period
        frames = sample["frames"]
        thread_samples[str(sample["tid"])] += 1
        frame_samples[f'{windows[index]["replay"]}:{windows[index]["round"]}'] += 1
        if not frames:
            missing += 1
            own["[missing stack]"] += period
            own_samples["[missing stack]"] += 1
            continue
        unknown += "[unknown]" in frames[0]
        unknown_ancestor += any("[unknown]" in frame for frame in frames[1:])
        shallow += len(frames) <= 1
        own[frames[0]] += period
        own_samples[frames[0]] += 1
        # 含子调用按每样本唯一符号计入，递归/内联重复不能多次计算同一函数
        for frame in set(frames):
            inclusive[frame] += period
        paths[" <- ".join(frames[:8])] += period
    def ranked(counter, counts=None):
        return [{"function": name, "event_weight": value, "percent": 100 * value / weight if weight else 0,
                 **({"self_samples": counts[name]} if counts is not None else {})}
                for name, value in counter.most_common(50)]
    return {"total_samples": total, "roi_samples": inside, "outside_samples": outside,
            "roi_event_weight": weight, "missing_stack_samples": missing,
            "unknown_self_samples": unknown, "single_frame_samples": shallow,
            "unknown_ancestor_samples": unknown_ancestor,
            "threads": dict(thread_samples), "windows": dict(frame_samples),
            "self": ranked(own, own_samples), "inclusive": ranked(inclusive), "paths": ranked(paths),
            "roi_wall_ms": sum(w["end_ns"] - w["start_ns"] for w in windows) / 1e6,
            "control_ms": sum(w["enable_ns"] + w["disable_ns"] for w in windows) / 1e6,
            "sample_screening_sufficient": inside >= 2000}


def markdown_perf(summary):
    lines = ["# 函数采样摘要", "", "采样事件为 cpu-clock:u；比例按 ROI 内实际 period 加权，不是精确函数调用计时。", "",
             f'窗口内样本 {summary["roi_samples"]}，窗口外 {summary["outside_samples"]}；'
             f'缺栈 {summary["missing_stack_samples"]}，未知自身符号 {summary["unknown_self_samples"]}。',
             f'ROI 墙钟合计 {summary["roi_wall_ms"]:.3f}ms，控制握手 {summary["control_ms"]:.3f}ms。', "",
             "| 函数自身热点 | 事件权重占比 | 自身样本 |", "| --- | ---: | ---: |"]
    for row in summary["self"][:20]:
        name = row["function"].replace("|", "\\|").replace("`", "'")
        confidence = "（少量样本）" if row["self_samples"] < 30 else ""
        lines.append(f'| `{name}` | {row["percent"]:.2f}% | {row["self_samples"]}{confidence} |')
    lines.extend(["", "## 含子调用热点", "", "父子百分比不可相加；缺失调用者可能低估包含成本。", ""])
    for row in summary["inclusive"][:15]:
        lines.append(f'- `{row["function"]}`：{row["percent"]:.2f}%')
    return "\n".join(lines) + "\n"
