"""受控性能进程的 Windows 环境观测；只修改本采集器拥有的亲和性。"""

from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import os


class ProcessMemory(ctypes.Structure):
    """对应 Windows 进程内存计数，进程结束后仍可通过保留句柄读取峰值。"""

    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
            "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]


class CacheDescriptor(ctypes.Structure):
    _fields_ = [("Level", ctypes.c_byte), ("Associativity", ctypes.c_byte),
                ("LineSize", ctypes.c_ushort), ("Size", wintypes.DWORD), ("Type", ctypes.c_int)]


class ProcessorDetail(ctypes.Union):
    _fields_ = [("Cache", CacheDescriptor), ("Reserved", ctypes.c_ulonglong * 2)]


class ProcessorInfo(ctypes.Structure):
    _fields_ = [("ProcessorMask", ctypes.c_size_t), ("Relationship", ctypes.c_int),
                ("Detail", ProcessorDetail)]


class WindowsEnvironment:
    """拥有 API 绑定，不拥有被测子进程；多处理器组机器显式拒绝旧式掩码协议。"""

    def __init__(self):
        if os.name != "nt":
            raise RuntimeError("此环境采集协议仅支持 Windows")
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.GetCurrentProcess.restype = wintypes.HANDLE
        self.kernel.GetProcessAffinityMask.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(ctypes.c_size_t)] * 2
        self.kernel.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
        self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.kernel.GetSystemTimes.argtypes = [ctypes.POINTER(wintypes.FILETIME)] * 3
        self.kernel.QueryProcessCycleTime.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_ulonglong)]
        self.kernel.GetPriorityClass.argtypes = [wintypes.HANDLE]
        self.kernel.GetLogicalProcessorInformation.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.GetActiveProcessorGroupCount.restype = wintypes.WORD
        self.memory_query = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
        self.memory_query.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessMemory), wintypes.DWORD]
        if self.kernel.GetActiveProcessorGroupCount() != 1:
            raise RuntimeError("当前协议不支持多个 Windows 处理器组，不能以截断掩码继续")
        self.handle = self.kernel.GetCurrentProcess()

    @staticmethod
    def checked(result):
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())
        return result

    @staticmethod
    def ticks(value):
        return (value.dwHighDateTime << 32) | value.dwLowDateTime

    def affinity(self, handle=None):
        process, system = ctypes.c_size_t(), ctypes.c_size_t()
        self.checked(self.kernel.GetProcessAffinityMask(
            self.handle if handle is None else handle, ctypes.byref(process), ctypes.byref(system)))
        return process.value, system.value

    def topology(self):
        size = wintypes.DWORD()
        self.kernel.GetLogicalProcessorInformation(None, ctypes.byref(size))
        if size.value == 0:
            raise ctypes.WinError(ctypes.get_last_error())
        records = (ProcessorInfo * (size.value // ctypes.sizeof(ProcessorInfo)))()
        self.checked(self.kernel.GetLogicalProcessorInformation(records, ctypes.byref(size)))
        return {"processAffinity": self.affinity()[0], "systemAffinity": self.affinity()[1],
                "l3Groups": [{"mask": row.ProcessorMask, "bytes": row.Detail.Cache.Size}
                             for row in records if row.Relationship == 2 and row.Detail.Cache.Level == 3]}

    @contextmanager
    def scoped_affinity(self, requested=None):
        original, available = self.affinity()
        target = original if requested is None else requested
        if target <= 0 or target & ~available:
            raise ValueError("亲和性必须是当前系统可用处理器的非空子集")
        try:
            self.checked(self.kernel.SetProcessAffinityMask(self.handle, target))
            yield target
        finally:
            self.checked(self.kernel.SetProcessAffinityMask(self.handle, original))
            if self.affinity()[0] != original:
                raise RuntimeError("采集器亲和性未恢复")

    def system_times(self):
        values = [wintypes.FILETIME() for _ in range(3)]
        self.checked(self.kernel.GetSystemTimes(*(ctypes.byref(value) for value in values)))
        return [self.ticks(value) for value in values]

    @staticmethod
    def system_busy(before, after):
        total = after[1] + after[2] - before[1] - before[2]
        return 100 * (1 - (after[0] - before[0]) / total) if total > 0 else None

    def process_counters(self, handle):
        values = [wintypes.FILETIME() for _ in range(4)]
        self.checked(self.kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in values)))
        cycles = ctypes.c_ulonglong()
        self.checked(self.kernel.QueryProcessCycleTime(handle, ctypes.byref(cycles)))
        memory = ProcessMemory()
        memory.cb = ctypes.sizeof(memory)
        self.checked(self.memory_query(handle, ctypes.byref(memory), memory.cb))
        return {"cpuSeconds": (self.ticks(values[2]) + self.ticks(values[3])) / 1e7,
                "cpuCycles": cycles.value, "peakWorkingSetBytes": memory.PeakWorkingSetSize,
                "priorityClass": self.checked(self.kernel.GetPriorityClass(handle))}
