"""由 Ubuntu 编排有限原生平台回放，身份、失败、配额与离线评价分别归档。"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
KEYFRAMES = (0, 2, 15, 16, 23)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def identity(path):
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def win(path):
    return subprocess.check_output(["wslpath", "-w", str(path.resolve())], text=True).strip()


def quote(text):
    return "'" + text.replace("'", "''") + "'"


def native_run(output, name, exe, case, algorithm, workers, mode):
    record = output / "commands" / f"{name}.json"
    if record.exists():
        return json.loads(record.read_text())
    consumed = sum(json.loads(p.read_text()).get("elapsedSeconds", 0)
                   for p in (output / "commands").glob("*.json"))
    if consumed >= 1200:
        write(record, {"status": "not-run-total-cap"})
        return {"status": "not-run-total-cap"}
    logs = output / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    args = ["--transactional-platform-replay", case, algorithm, str(workers), win(output / name), mode]
    # 只控制本次创建的进程；不用 Linux RSS/affinity 冒充原生 Windows 指标
    helper = logs / f"{name}.ps1"
    helper.write_text("\n".join([
        "$ErrorActionPreference = 'Stop'",
        f"$taskProcess = Start-Process -FilePath {quote(win(exe))} -ArgumentList {quote(subprocess.list2cmdline(args))} "
        f"-WorkingDirectory {quote(win(ROOT))} -WindowStyle Hidden -PassThru "
        f"-RedirectStandardOutput {quote(win(logs / (name + '.stdout')))} "
        f"-RedirectStandardError {quote(win(logs / (name + '.stderr')))}",
        "$null = $taskProcess.Handle",
        "$watch = [Diagnostics.Stopwatch]::StartNew(); $peak = 0L; $reason = 'completed'",
        "while (-not $taskProcess.WaitForExit(100)) {",
        "  $taskProcess.Refresh(); $peak = [Math]::Max($peak, $taskProcess.PeakWorkingSet64)",
        "  if ($peak -ge 8589934592 -or $watch.Elapsed.TotalSeconds -ge 180) {",
        "    $reason = 'censored'; $taskProcess.Kill(); break",
        "  }",
        "}",
        "$taskProcess.WaitForExit(); $taskProcess.Refresh()",
        "[PSCustomObject]@{returncode=$taskProcess.ExitCode; elapsedSeconds=$watch.Elapsed.TotalSeconds; "
        "peakRssBytes=$peak; status=$reason} | ConvertTo-Json -Compress",
    ]) + "\n", encoding="utf-8-sig")
    start = time.time()
    result = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", win(helper)],
                            capture_output=True, text=True, timeout=200)
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        data = {"status": "launcher-failed", "returncode": result.returncode,
                "elapsedSeconds": time.time() - start, "stdout": result.stdout, "stderr": result.stderr}
    if data["status"] == "completed":
        data["status"] = "ok" if data["returncode"] == 0 else "failed"
    data.update(argv=[win(exe), *args], binary=identity(exe))
    write(record, data)
    print(name, data["status"], round(data["elapsedSeconds"], 2), flush=True)
    return data


def collect(output, exe, backend):
    freeze = output / f"{backend}-freeze.json"
    if not freeze.exists():
        files = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z",
                                        "src", "tests", "scripts", "cmake", "CMakeLists.txt"], cwd=ROOT).decode().split("\0")
        write(freeze, {"protocol": "tpi05-v1", "backend": backend, "binary": identity(exe),
                       "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                       "sources": {f: identity(ROOT / f) for f in sorted(set(files)) if f and (ROOT / f).is_file()},
                       "assets": [identity(ROOT / "assets/heightmaps" / n) for n in
                                  ("Hm_Terrain_Test_129.pgm", "Hm_Terrain_Peking_513.png")],
                       "affinity": "native default; requested workers are not measured active cores",
                       "views": [14,14,14,15,16,18,22,26,30,34,38,42,46,50,54,58,14,14,14,14,14,14,14,14]})
    elif json.loads(freeze.read_text())["binary"]["sha256"] != identity(exe)["sha256"]:
        raise RuntimeError("Frozen executable changed")
    cases = ("test129", "peking") if backend == "opengl" else ("peking",)
    for case in cases:
        configs = [("transactional", 1), ("transactional", 8), ("dod", 8)] if backend == "opengl" else [("transactional", 8), ("dod", 8)]
        if backend == "opengl" and case == "peking":
            configs.append(("classic", 8))
        for algorithm, workers in configs:
            name = f"{backend}-{case}-{algorithm}{workers}-normal"
            native_run(output, name, exe, case, algorithm, workers, "normal")
    # 相同矩阵单独再走一遍，只承担关键帧输出；不混入正常时间
    if backend == "opengl":
        for case in cases:
            for algorithm in (("transactional", "dod", "classic") if case == "peking" else ("transactional", "dod")):
                normal = output / f"commands/{backend}-{case}-{algorithm}8-normal.json"
                if json.loads(normal.read_text())["status"] != "ok":
                    continue
                name = f"{backend}-{case}-{algorithm}8-export"
                native_run(output, name, exe, case, algorithm, 8, "export")


def quality(output, probe):
    for directory in sorted(output.glob("opengl-*-export")):
        asset = "Hm_Terrain_Test_129.pgm" if "test129" in directory.name else "Hm_Terrain_Peking_513.png"
        for frame in KEYFRAMES:
            mesh = directory / f"mesh-{frame}.bin"
            if not mesh.exists() or (directory / f"quality-{frame}").exists():
                continue
            argv = [str(probe), str(mesh), str(ROOT / "assets/heightmaps" / asset), str(directory / f"quality-{frame}")]
            started = time.time()
            try:
                result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=180)
                data = {"argv": argv, "returncode": result.returncode, "elapsedSeconds": time.time()-started,
                        "stdout": result.stdout, "stderr": result.stderr, "binary": identity(probe)}
            except subprocess.TimeoutExpired:
                data = {"argv": argv, "status": "censored-time"}
            write(output / "quality-commands" / f"{directory.name}-{frame}.json", data)
            print(directory.name, frame, data.get("returncode", data.get("status")), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("collect", "quality"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--backend", choices=("opengl", "d3d12"), default="opengl")
    args = parser.parse_args()
    if args.mode == "collect":
        collect(args.output.resolve(), args.exe.resolve(), args.backend)
    else:
        quality(args.output.resolve(), args.exe.resolve())
