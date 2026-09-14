"""只读取官方展开区间，以业务帧关联线程；区间墙钟不等于 CPU 时间。"""

from collections import defaultdict
import csv
import io


def read_zones(text):
    reader = csv.DictReader(io.StringIO(text))
    required = {"name", "thread", "ns_since_start", "exec_time_ns", "value"}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError("Tracy 区间字段缺失")
    result = []
    for row in reader:
        if None in row or any(row.get(key) is None for key in required):
            raise ValueError("Tracy 区间行被截断")
        start, duration = int(row["ns_since_start"]), int(row["exec_time_ns"])
        if start < 0 or duration < 0:
            raise ValueError("Tracy 区间时间非法")
        result.append({"name": row["name"], "thread": row["thread"], "start": start,
                       "end": start + duration, "duration": duration, "text": row["value"],
                       "file": row.get("src_file"), "line": row.get("src_line")})
    return result


def contains(parent, child):
    return parent["start"] <= child["start"] and child["end"] <= parent["end"]


def summarize_tracy(text, expected_frames):
    zones = read_zones(text)
    frames = sorted((z for z in zones if z["name"] == "profile.frame"), key=lambda z: z["start"])
    complete = [z for z in zones if z["name"] == "profile.complete"]
    if len(frames) != expected_frames or len(complete) != 1 or not frames:
        raise ValueError("Tracy 缺少完整帧或唯一结束标志")
    if complete[0]["start"] < frames[-1]["end"]:
        raise ValueError("Tracy 完成标志早于最后一帧")
    if len({f["text"] for f in frames}) != expected_frames:
        raise ValueError("Tracy 帧身份缺失或重复")
    if any(a["end"] > b["start"] for a, b in zip(frames, frames[1:])):
        raise ValueError("Tracy 帧窗口重叠")
    all_tasks = [z for z in zones if z["name"] == "gtp.task"]
    if any(sum(contains(f, t) for f in frames) != 1 for t in all_tasks):
        raise ValueError("业务任务未被唯一完整帧覆盖")
    aggregate = defaultdict(lambda: {"calls": 0, "inclusive_ns": 0, "self_ns": 0})
    frame_results = []
    for frame in frames:
        selected = [z for z in zones if contains(frame, z)]
        by_thread = defaultdict(list)
        for z in selected:
            entry = aggregate[z["name"]]
            entry["calls"] += 1
            entry["inclusive_ns"] += z["duration"]
            entry["self_ns"] += z["duration"]
            entry["file"], entry["line"] = z["file"], z["line"]
            by_thread[z["thread"]].append(z)
        # 仅减同线程直接标记子区间；未标记库调用仍属于该 zone 的 self
        for values in by_thread.values():
            stack = []
            for z in sorted(values, key=lambda z: (z["start"], -z["end"])):
                while stack and z["start"] >= stack[-1]["end"]:
                    stack.pop()
                if stack:
                    if not contains(stack[-1], z):
                        raise ValueError("同线程区间交叉，不能构成合法作用域")
                    aggregate[stack[-1]["name"]]["self_ns"] -= z["duration"]
                stack.append(z)
        phases = []
        for dispatch in (z for z in selected if z["name"] == "gtp.dispatch"):
            tasks = [z for z in selected if z["name"] == "gtp.task" and
                     z["text"].rsplit("/", 1)[0] == dispatch["text"] and contains(dispatch, z)]
            if len({t["text"] for t in tasks}) != len(tasks):
                raise ValueError("派发内存在重复任务身份")
            waits = [z for z in selected if z["name"] == "pool.wait" and contains(dispatch, z)
                     and z["thread"] == dispatch["thread"]]
            enqueue = [z for z in selected if z["name"] == "pool.enqueue" and contains(dispatch, z)
                       and z["thread"] == dispatch["thread"]]
            phases.append({"phase": dispatch["text"], "wall_ns": dispatch["duration"],
                           "tasks": len(tasks), "threads": sorted({t["thread"] for t in tasks}),
                           "task_wall_sum_ns": sum(t["duration"] for t in tasks),
                           "task_max_ns": max((t["duration"] for t in tasks), default=0),
                           "wait_ns": sum(w["duration"] for w in waits),
                           "after_last_task_ns": dispatch["end"] - max((t["end"] for t in tasks), default=dispatch["end"]),
                           "enqueue_begin_to_first_task_ns": min(t["start"] for t in tasks) - enqueue[0]["start"]
                           if tasks and enqueue else None})
        frame_results.append({"identity": frame["text"], "wall_ns": frame["duration"],
                              "zones": len(selected), "phases": phases,
                              "main_stage_ns": {name: sum(z["duration"] for z in selected if z["name"] == name and
                                                          z["thread"] == frame["thread"])
                                                for name in ("gtp.set_view", "gtp.update", "gtp.reservation",
                                                             "gtp.samples.prepare", "gtp.commit.publish", "dod.build",
                                                             "dod.merge_score", "dod.merge_topology", "dod.split_score",
                                                             "dod.split_topology", "dod.mesh", "classic.build_packet")}})
    return {"complete": True, "total_zones": len(zones), "roi_zones": sum(f["zones"] for f in frame_results),
            "frames": frame_results, "functions": dict(aggregate),
            "task_threads": sorted({t["thread"] for t in all_tasks}),
            "roi_pool_threads": sorted({z["thread"] for z in zones if z["name"] == "pool.task" and
                                        any(contains(f, z) for f in frames)}),
            "thread_id_semantics": "Tracy export thread ID; not OS TID"}


def markdown_tracy(summary):
    lines = ["# 函数与线程时间线", "", "区间为墙钟；同线程嵌套与跨线程工作不可重复求和作为帧时间。", "",
             "| 帧 | 窗口 ms | 标记数 | 视图刷新 ms | 更新或家族构建 ms |", "| --- | ---: | ---: | ---: | ---: |"]
    for f in summary["frames"]:
        update = next((f["main_stage_ns"].get(name, 0) for name in
                       ("gtp.update", "dod.build", "classic.build_packet") if f["main_stage_ns"].get(name, 0)), None)
        update_text = f"{update/1e6:.3f}" if update is not None else "未标记该入口"
        lines.append(f'| {f["identity"]} | {f["wall_ns"]/1e6:.3f} | {f["zones"]} | '
                     f'{f["main_stage_ns"]["gtp.set_view"]/1e6:.3f} | {update_text} |')
    lines.extend(["", "## 函数区间", "", "self 仅扣已标记同线程子区间，仍包含未标记的下层函数。", "",
                  "| 区间 | 次数 | 含子区间 ms | self ms |", "| --- | ---: | ---: | ---: |"])
    for name, entry in sorted(summary["functions"].items(), key=lambda item: -item[1]["inclusive_ns"]):
        lines.append(f'| `{name}` | {entry["calls"]} | {entry["inclusive_ns"]/1e6:.3f} | {entry["self_ns"]/1e6:.3f} |')
    return "\n".join(lines) + "\n"
