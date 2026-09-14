"""控制独立 perf 进程及官方导出；算法语义由原探针负责。"""

import os
import subprocess
import time

from profiling.process import stop_process
from profiling.environment import command


def record(perf, executable, arguments, work, cwd, timeout=180, frequency=499, callgraph="fp", fixture=False):
    control, ack = work / "control.fifo", work / "ack.fifo"
    for path in (control, ack):
        os.mkfifo(path, mode=0o600)
    env = dict(os.environ, ROAM_PERF_CONTROL=str(control), ROAM_PERF_ACK=str(ack), DEBUGINFOD_URLS="")
    if fixture:
        env["ROAM_PROFILE_WINDOWS"] = str(work / "profile-windows.csv")
    data = work / "perf.data"
    argv = [str(perf), "record", "-e", "cpu-clock:u", "-F", str(frequency),
            "--call-graph", callgraph, "--clockid", "mono", "--delay=-1",
            f"--control=fifo:{control},{ack}", "-o", str(data), "--", str(executable), *arguments]
    started = time.monotonic()
    result = {"argv": argv, "clock": "CLOCK_MONOTONIC", "event": "cpu-clock:u", "callgraph": callgraph}
    process = None
    with (work / "target.log").open("w") as stdout, (work / "perf-record.log").open("w") as stderr:
        try:
            process = subprocess.Popen(argv, env=env, cwd=cwd, stdout=stdout, stderr=stderr, start_new_session=True)
            while process.poll() is None:
                if time.monotonic() - started > timeout:
                    raise TimeoutError("采集超过总墙钟上限")
                if data.exists() and data.stat().st_size > 512 * 1024 * 1024:
                    raise RuntimeError("perf.data 超过 512MiB 上限")
                time.sleep(0.1)
            result.update(returncode=process.returncode, complete=process.returncode == 0)
        except (OSError, TimeoutError, RuntimeError) as error:
            result.update(complete=False, error=str(error))
        finally:
            if process is not None:
                stop_process(process)
    result["elapsed_seconds"] = time.monotonic() - started
    return result


def export(perf, work):
    """按真实记录的时钟和符号导出，不对失败记录补造调用栈。"""
    data = str(work / "perf.data")
    commands = {
        "perf-script.txt": [str(perf), "script", "--no-inline", "--ns", "-i", data,
                            "-F", "comm,pid,tid,time,period,event,ip,sym,dso"],
        "perf-report.txt": [str(perf), "report", "--stdio", "--children", "--percent-limit", "0.1", "-i", data],
        "perf-header.txt": [str(perf), "report", "--stdio", "--header-only", "-i", data],
        "build-ids.txt": [str(perf), "buildid-list", "-i", data],
    }
    results = {}
    for name, argv in commands.items():
        result = command(argv, timeout=45, env=dict(os.environ, DEBUGINFOD_URLS=""))
        (work / name).write_text(result["stdout"], encoding="utf-8")
        results[name] = {k: v for k, v in result.items() if k != "stdout"}
    return results
