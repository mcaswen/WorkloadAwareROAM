#!/usr/bin/env python3
"""
Export project code files into one text snapshot with line statistics.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


CODE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cmake",
    ".comp",
    ".cpp",
    ".cxx",
    ".frag",
    ".geom",
    ".glsl",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
    ".json",
    ".m",
    ".mm",
    ".ps1",
    ".py",
    ".sh",
    ".tesc",
    ".tese",
    ".vert",
}

CODE_FILENAMES = {
    "CMakeLists.txt",
}

DEFAULT_INCLUDE_ROOTS = (
    "src",
    "cmake",
    "scripts",
    "tests",
    "CMakeLists.txt",
    "CMakePresets.json",
    "vcpkg.json",
)

EXCLUDED_DIR_NAMES = {
    ".git",
    ".vscode",
    "_deps",
    "benchmark-output",
    "build",
    "captures",
    "code-output",
    "coverage",
    "experiment-output",
    "external",
    "logs",
    "out",
    "screenshots",
    "third_party",
}


@dataclass(frozen=True)
class FileStats:
    path: Path
    total_lines: int
    code_lines: int
    comment_lines: int
    blank_lines: int

    @property
    def coverage_percent(self) -> float:
        measured_lines = self.code_lines + self.comment_lines
        if measured_lines == 0:
            return 0.0
        return self.comment_lines * 100.0 / measured_lines


@dataclass(frozen=True)
class SnapshotStats:
    files: tuple[FileStats, ...]

    @property
    def file_count(self) -> int:
        return len(self.files)

    @property
    def total_lines(self) -> int:
        return sum(file.total_lines for file in self.files)

    @property
    def code_lines(self) -> int:
        return sum(file.code_lines for file in self.files)

    @property
    def comment_lines(self) -> int:
        return sum(file.comment_lines for file in self.files)

    @property
    def blank_lines(self) -> int:
        return sum(file.blank_lines for file in self.files)

    @property
    def coverage_percent(self) -> float:
        measured_lines = self.code_lines + self.comment_lines
        if measured_lines == 0:
            return 0.0
        return self.comment_lines * 100.0 / measured_lines


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export all project code files into one txt file with code/comment statistics."
    )
    parser.add_argument(
        "-o",
        "--output",
        default="code-output/code-snapshot.txt",
        help="Output txt path, relative to project root when not absolute",
    )
    parser.add_argument(
        "--root",
        default=None,
        help="Project root, defaults to the parent directory of scripts/",
    )
    return parser.parse_args()


def resolve_project_root(root_arg: str | None) -> Path:
    if root_arg is not None:
        return Path(root_arg).expanduser().resolve()
    return Path(__file__).resolve().parents[1]


def should_skip_dir(path: Path) -> bool:
    if path.name in EXCLUDED_DIR_NAMES:
        return True
    return path.name.startswith("cmake-build-")


def is_code_file(path: Path) -> bool:
    if path.name in CODE_FILENAMES:
        return True
    return path.suffix in CODE_EXTENSIONS


def iter_code_files(project_root: Path) -> Iterable[Path]:
    for include_root in DEFAULT_INCLUDE_ROOTS:
        root = project_root / include_root
        if not root.exists():
            continue

        if root.is_file():
            if is_code_file(root):
                yield root
            continue

        for path in root.rglob("*"):
            if any(should_skip_dir(parent) for parent in path.relative_to(project_root).parents):
                continue

            if path.is_file() and is_code_file(path):
                yield path


def comment_style(path: Path) -> str:
    if path.suffix in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".m", ".mm"}:
        return "cpp"
    if path.suffix in {".glsl", ".vert", ".frag", ".geom", ".comp", ".tesc", ".tese"}:
        return "cpp"
    if path.suffix in {".cmake", ".py", ".sh", ".ps1"} or path.name == "CMakeLists.txt":
        return "hash"
    return "none"


def classify_lines(path: Path, text: str) -> FileStats:
    style = comment_style(path)
    total_lines = 0
    code_lines = 0
    comment_lines = 0
    blank_lines = 0
    in_block_comment = False

    for line in text.splitlines():
        total_lines += 1
        stripped = line.strip()
        if not stripped:
            blank_lines += 1
            continue

        if style == "hash":
            if stripped.startswith("#") and not stripped.startswith("#!"):
                comment_lines += 1
            else:
                code_lines += 1
            continue

        if style == "cpp":
            if in_block_comment:
                comment_lines += 1
                if "*/" in stripped:
                    in_block_comment = False
                continue

            if stripped.startswith("//"):
                comment_lines += 1
                continue

            if stripped.startswith("/*"):
                comment_lines += 1
                if "*/" not in stripped:
                    in_block_comment = True
                continue

            if stripped.startswith("*") or stripped.startswith("*/"):
                comment_lines += 1
                if "*/" in stripped:
                    in_block_comment = False
                continue

        code_lines += 1

    return FileStats(
        path=path,
        total_lines=total_lines,
        code_lines=code_lines,
        comment_lines=comment_lines,
        blank_lines=blank_lines,
    )


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def relative_path(project_root: Path, path: Path) -> str:
    return path.relative_to(project_root).as_posix()


def write_header(output, project_root: Path, stats: SnapshotStats) -> None:
    output.write("Parallel ROAM Code Snapshot\n")
    output.write(f"Generated At: {datetime.now().isoformat(timespec='seconds')}\n")
    output.write(f"Project Root: {project_root}\n")
    output.write("\n")
    output.write("Summary\n")
    output.write(f"File Count: {stats.file_count}\n")
    output.write(f"Code Lines: {stats.code_lines}\n")
    output.write(f"Comment Lines: {stats.comment_lines}\n")
    output.write(f"Blank Lines: {stats.blank_lines}\n")
    output.write(f"Total Lines: {stats.total_lines}\n")
    output.write(f"Comment Coverage: {stats.coverage_percent:.2f}%\n")
    output.write("\n")
    output.write("Included Roots\n")
    for include_root in DEFAULT_INCLUDE_ROOTS:
        output.write(f"- {include_root}\n")
    output.write("\n")
    output.write("Excluded Directories\n")
    for directory in sorted(EXCLUDED_DIR_NAMES):
        output.write(f"- {directory}\n")
    output.write("\n")
    output.write("File Index\n")


def write_file_index(output, project_root: Path, stats: SnapshotStats) -> None:
    for index, file_stats in enumerate(stats.files, start=1):
        output.write(
            f"{index:03d}. {relative_path(project_root, file_stats.path)} "
            f"(total={file_stats.total_lines}, code={file_stats.code_lines}, "
            f"comments={file_stats.comment_lines}, blank={file_stats.blank_lines}, "
            f"coverage={file_stats.coverage_percent:.2f}%)\n"
        )
    output.write("\n")


def write_file_contents(output, project_root: Path, file_stats: FileStats) -> None:
    separator = "=" * 96
    path_text = relative_path(project_root, file_stats.path)
    output.write(f"{separator}\n")
    output.write(f"FILE: {path_text}\n")
    output.write(
        f"LINES: total={file_stats.total_lines}, code={file_stats.code_lines}, "
        f"comments={file_stats.comment_lines}, blank={file_stats.blank_lines}, "
        f"coverage={file_stats.coverage_percent:.2f}%\n"
    )
    output.write(f"{separator}\n")
    text = read_text(file_stats.path)
    output.write(text)
    if file_stats.total_lines == 0 or not text.endswith("\n"):
        output.write("\n")
    output.write("\n")


def export_snapshot(project_root: Path, output_path: Path) -> SnapshotStats:
    files = tuple(
        classify_lines(path, read_text(path))
        for path in sorted(set(iter_code_files(project_root)), key=lambda item: item.relative_to(project_root).as_posix())
    )
    stats = SnapshotStats(files=files)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as output:
        write_header(output, project_root, stats)
        write_file_index(output, project_root, stats)
        for file_stats in stats.files:
            write_file_contents(output, project_root, file_stats)

    return stats


def main() -> int:
    args = parse_args()
    project_root = resolve_project_root(args.root)
    output_path = Path(args.output).expanduser()
    if not output_path.is_absolute():
        output_path = project_root / output_path

    stats = export_snapshot(project_root, output_path)
    print(f"Code snapshot written: {output_path}")
    print(
        f"files={stats.file_count} code={stats.code_lines} comments={stats.comment_lines} "
        f"blank={stats.blank_lines} coverage={stats.coverage_percent:.2f}%"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
