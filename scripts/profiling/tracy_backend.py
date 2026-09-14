"""官方 Tracy 采集/导出进程；连接和业务完整性由会话与报告共同验证。"""

import os
import subprocess
import time

from profiling.environment import command
from profiling.process import stop_process


def capture_tracy(capture, executable, arguments, work, timeout=180, cwd=None):
    trace = work / "capture.tracy"
    processes = []
    started = time.monotonic()
    capture_argv = [str(capture), "-a", "127.0.0.1", "-o", str(trace)]
    target_argv = [str(executable), *arguments]
    result = {"capture_argv": capture_argv, "target_argv": target_argv, "complete_exit": False}
    with (work / "capture.log").open("w") as collector_log, (work / "target.log").open("w") as target_log:
        try:
            collector = subprocess.Popen(capture_argv, stdout=collector_log, stderr=subprocess.STDOUT,
                                         start_new_session=True)
            processes.append(collector)
            env = dict(os.environ, ROAM_PROFILE_WAIT="1", ROAM_PROFILE_BACKEND="tracy", TRACY_NO_EXIT="1")
            target = subprocess.Popen(target_argv, env=env, cwd=cwd, stdout=target_log,
                                      stderr=subprocess.STDOUT, start_new_session=True)
            processes.append(target)
            while target.poll() is None:
                if collector.poll() is not None:
                    raise RuntimeError("采集器提前退出")
                if time.monotonic() - started > timeout:
                    raise TimeoutError("目标与采集超过总期限")
                if trace.exists() and trace.stat().st_size > 512 * 1024 * 1024:
                    raise RuntimeError("trace 超过 512MiB 限制")
                time.sleep(.05)
            collector.wait(timeout=10)
            if trace.exists() and trace.stat().st_size > 512 * 1024 * 1024:
                raise RuntimeError("最终 trace 超过 512MiB 限制")
            result.update(target_returncode=target.returncode, capture_returncode=collector.returncode,
                          complete_exit=target.returncode == 0 and collector.returncode == 0)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            result["error"] = str(error)
        finally:
            for process in reversed(processes):
                stop_process(process)
    result["elapsed_seconds"] = time.monotonic() - started
    return result


def export(csvexport, work):
    result = {}
    for name, options in (("zones.csv", ["-u"]), ("zones-self.csv", ["-u", "-e"])):
        value = command([str(csvexport), *options, str(work / "capture.tracy")], timeout=45)
        (work / name).write_text(value["stdout"], encoding="utf-8")
        result[name] = {k: v for k, v in value.items() if k != "stdout"}
    return result
