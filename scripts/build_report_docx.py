from __future__ import annotations

import html
import re
import zipfile
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
REPORT_MD = ROOT / "docs" / "parallel-roam" / "final-technical-report.md"
OUTPUT_DOCX = ROOT / "docs" / "parallel-roam" / "final-technical-report.docx"

NS_W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS_R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
NS_WP = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
NS_A = "http://schemas.openxmlformats.org/drawingml/2006/main"
NS_PIC = "http://schemas.openxmlformats.org/drawingml/2006/picture"
NS_M = "http://schemas.openxmlformats.org/officeDocument/2006/math"
NS_REL = "http://schemas.openxmlformats.org/package/2006/relationships"

EMU_PER_INCH = 914400
CM_PER_INCH = 2.54
MAX_IMAGE_WIDTH_EMU = int(15.2 / CM_PER_INCH * EMU_PER_INCH)
GENERATED_DATE = date.today()
SUBSCRIPT_OPEN = "\ue100"
SUBSCRIPT_CLOSE = "\ue101"


@dataclass
class ImageRel:
    rid: str
    docx_path: str
    source_path: Path
    width_emu: int
    height_emu: int
    alt: str


def esc(text: str) -> str:
    return html.escape(text, quote=True)


def xml_space_attr(text: str) -> str:
    return ' xml:space="preserve"' if text.startswith(" ") or text.endswith(" ") else ""


SUBSCRIPT_MAP = str.maketrans({
    "0": "₀",
    "1": "₁",
    "2": "₂",
    "3": "₃",
    "4": "₄",
    "5": "₅",
    "6": "₆",
    "7": "₇",
    "8": "₈",
    "9": "₉",
    "+": "₊",
    "-": "₋",
    "=": "₌",
    "(": "₍",
    ")": "₎",
    "a": "ₐ",
    "e": "ₑ",
    "h": "ₕ",
    "i": "ᵢ",
    "j": "ⱼ",
    "k": "ₖ",
    "l": "ₗ",
    "m": "ₘ",
    "n": "ₙ",
    "o": "ₒ",
    "p": "ₚ",
    "r": "ᵣ",
    "s": "ₛ",
    "t": "ₜ",
    "u": "ᵤ",
    "v": "ᵥ",
    "x": "ₓ",
})

SUPERSCRIPT_MAP = str.maketrans({
    "0": "⁰",
    "1": "¹",
    "2": "²",
    "3": "³",
    "4": "⁴",
    "5": "⁵",
    "6": "⁶",
    "7": "⁷",
    "8": "⁸",
    "9": "⁹",
    "+": "⁺",
    "-": "⁻",
    "=": "⁼",
    "(": "⁽",
    ")": "⁾",
    "d": "ᵈ",
    "i": "ⁱ",
    "j": "ʲ",
    "k": "ᵏ",
    "n": "ⁿ",
    "r": "ʳ",
})


def convert_script_text(text: str, mapping: dict[int, str], fallback_prefix: str) -> str:
    converted = text.translate(mapping)
    fully_convertible = all((not char.isascii()) or (not char.isalnum()) or ord(char) in mapping for char in text)
    if fully_convertible and len(converted) == len(text) and converted != text:
        return converted
    return f"{fallback_prefix}{text}"


def replace_grouped_command(text: str, command: str, formatter) -> str:
    marker = f"\\{command}"
    while marker + "{" in text:
        start = text.find(marker + "{")
        group_start = start + len(marker)
        content, end = read_braced_group(text, group_start)
        if content is None:
            break
        text = text[:start] + formatter(content) + text[end:]
    return text


def read_braced_group(text: str, start: int) -> tuple[str | None, int]:
    if start >= len(text) or text[start] != "{":
        return None, start
    depth = 0
    for index in range(start, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index], index + 1
    return None, start


def replace_fractions(text: str) -> str:
    marker = r"\frac"
    while marker + "{" in text:
        start = text.find(marker + "{")
        numerator, numerator_end = read_braced_group(text, start + len(marker))
        if numerator is None:
            break
        denominator, denominator_end = read_braced_group(text, numerator_end)
        if denominator is None:
            break
        numerator_text = latex_to_math_text(numerator)
        denominator_text = latex_to_math_text(denominator)
        if re.fullmatch(r"[\w\u0370-\u03ff₀-₉⁰-⁹]+", numerator_text):
            left = numerator_text
        else:
            left = f"({numerator_text})"
        if re.fullmatch(r"[\w\u0370-\u03ff₀-₉⁰-⁹]+", denominator_text):
            right = denominator_text
        else:
            right = f"({denominator_text})"
        text = text[:start] + f"{left}/{right}" + text[denominator_end:]
    return text


def subscript_marker(text: str) -> str:
    return f"{SUBSCRIPT_OPEN}{latex_to_math_text(text)}{SUBSCRIPT_CLOSE}"


def replace_scripts(text: str) -> str:
    placeholders: dict[str, str] = {}
    pattern = re.compile(r"_\{([^{}]+)\}")

    def repl_subscript_group(match: re.Match[str]) -> str:
        key = f"\ue000{len(placeholders)}\ue001"
        placeholders[key] = subscript_marker(match.group(1))
        return key

    text = pattern.sub(repl_subscript_group, text)
    text = re.sub(r"_([A-Za-z0-9+\-=()])", lambda match: subscript_marker(match.group(1)), text)
    for key, value in placeholders.items():
        text = text.replace(key, value)

    for marker, mapping, fallback in [("^", SUPERSCRIPT_MAP, "^")]:
        placeholders = {}
        pattern = re.compile(rf"\{marker}\{{([^{{}}]+)\}}")

        def repl_group(match: re.Match[str]) -> str:
            key = f"\ue000{len(placeholders)}\ue001"
            placeholders[key] = convert_script_text(match.group(1), mapping, fallback)
            return key

        text = pattern.sub(repl_group, text)
        pattern = re.compile(rf"\{marker}([A-Za-z0-9+\-=()])")
        text = pattern.sub(lambda match: convert_script_text(match.group(1), mapping, fallback), text)
        for key, value in placeholders.items():
            text = text.replace(key, value)
    return text


def latex_to_math_text(text: str) -> str:
    text = text.strip()
    text = re.sub(r"\\text\{([^{}]*)\}", r"\1", text)
    text = re.sub(r"\\hat\{([^{}]+)\}", lambda match: f"{latex_to_math_text(match.group(1))}\u0302", text)
    text = re.sub(r"\\tilde\{([^{}]+)\}", lambda match: f"{latex_to_math_text(match.group(1))}\u0303", text)
    text = replace_fractions(text)

    replacements = {
        r"\left": "",
        r"\right": "",
        r"\cdots": "⋯",
        r"\ldots": "…",
        r"\alpha": "α",
        r"\beta": "β",
        r"\gamma": "γ",
        r"\lambda": "λ",
        r"\theta": "θ",
        r"\tau": "τ",
        r"\epsilon": "ε",
        r"\Delta": "Δ",
        r"\Omega": "Ω",
        r"\partial": "∂",
        r"\cup": "∪",
        r"\cap": "∩",
        r"\sim": "∼",
        r"\cdot": "·",
        r"\times": "×",
        r"\approx": "≈",
        r"\ge": "≥",
        r"\le": "≤",
        r"\ne": "≠",
        r"\neq": "≠",
        r"\in": "∈",
        r"\varnothing": "∅",
        r"\Rightarrow": "⇒",
        r"\max": "max",
        r"\tan": "tan",
        r"\sum": "Σ",
        r"\lfloor": "⌊",
        r"\rfloor": "⌋",
        r"\lceil": "⌈",
        r"\rceil": "⌉",
        r"\qquad": "   ",
        r"\quad": "  ",
        r"\{": "{",
        r"\}": "}",
        r"\|": "‖",
        r"\_": "_",
        r"\ ": " ",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)

    text = text.replace("&", "")
    text = replace_scripts(text)
    text = text.replace("{", "").replace("}", "")
    text = re.sub(r"\s+", " ", text).strip()
    return text


def strip_latex_line_break(text: str) -> str:
    return re.sub(r"\s*\\\\\s*$", "", text.strip())


def append_latex_rows(rows: list[str], text: str) -> None:
    for part in re.split(r"\\\\", text):
        stripped = part.strip()
        if stripped:
            rows.append(stripped)


def latex_matrix_lines(math_text: str, environment: str) -> list[str]:
    lines = math_text.splitlines()
    rows: list[str] = []
    prefix_parts: list[str] = []
    prefix = ""
    in_matrix = False
    begin_marker = rf"\begin{{{environment}}}"
    end_marker = rf"\end{{{environment}}}"
    for line in lines:
        stripped = line.strip()
        if begin_marker in stripped:
            before, after = stripped.split(begin_marker, 1)
            if before.strip():
                prefix_parts.append(strip_latex_line_break(before))
            prefix = latex_to_math_text(" ".join(prefix_parts))
            in_matrix = True
            if after.strip():
                append_latex_rows(rows, strip_latex_line_break(after))
            continue
        if end_marker in stripped:
            before = stripped.split(end_marker, 1)[0].strip()
            if before:
                append_latex_rows(rows, strip_latex_line_break(before))
            in_matrix = False
            continue
        if in_matrix:
            append_latex_rows(rows, strip_latex_line_break(stripped))
        elif stripped:
            prefix_parts.append(strip_latex_line_break(stripped))

    converted_rows = [latex_to_math_text(row) for row in rows if row.strip()]
    if not converted_rows:
        return [prefix] if prefix else []
    result = [f"{prefix} [" if prefix else "["]
    result.extend(f"  {row}" for row in converted_rows)
    result.append("]")
    return result


def latex_cases_lines(math_text: str) -> list[str]:
    lines = math_text.splitlines()
    rows: list[str] = []
    prefix_parts: list[str] = []
    prefix = ""
    in_cases = False
    for line in lines:
        stripped = line.strip()
        if r"\begin{cases}" in stripped:
            before, after = stripped.split(r"\begin{cases}", 1)
            if before.strip():
                prefix_parts.append(strip_latex_line_break(before))
            prefix = latex_to_math_text(" ".join(prefix_parts))
            in_cases = True
            if after.strip():
                rows.append(strip_latex_line_break(after))
            continue
        if r"\end{cases}" in stripped:
            before = stripped.split(r"\end{cases}", 1)[0].strip()
            if before:
                rows.append(strip_latex_line_break(before))
            in_cases = False
            continue
        if in_cases:
            rows.append(strip_latex_line_break(stripped))
        elif stripped:
            prefix_parts.append(strip_latex_line_break(stripped))
    converted_rows = [latex_to_math_text(row.replace("&", "  ")) for row in rows if row.strip()]
    return [f"{prefix} {{" if prefix else "{", *(f"  {row}" for row in converted_rows), "}"]


def latex_aligned_lines(math_text: str) -> list[str]:
    lines: list[str] = []
    for line in math_text.splitlines():
        stripped = line.strip()
        if stripped in {r"\begin{aligned}", r"\end{aligned}"}:
            continue
        if stripped:
            lines.append(latex_to_math_text(strip_latex_line_break(stripped)))
    return lines


def latex_to_display_lines(math_text: str) -> list[str]:
    if r"\begin{bmatrix}" in math_text:
        return latex_matrix_lines(math_text, "bmatrix")
    if r"\begin{cases}" in math_text:
        return latex_cases_lines(math_text)
    if r"\begin{aligned}" in math_text:
        return latex_aligned_lines(math_text)
    lines: list[str] = []
    for raw_line in math_text.splitlines():
        for part in re.split(r"\\\\", raw_line):
            stripped = part.strip()
            if stripped:
                lines.append(latex_to_math_text(stripped))
    return lines or [latex_to_math_text(math_text)]


def w_run(text: str, *, bold: bool = False, italic: bool = False, style: str | None = None) -> str:
    props = []
    if style:
        props.append(f'<w:rStyle w:val="{style}"/>')
    if bold:
        props.append("<w:b/>")
    if italic:
        props.append("<w:i/>")
    prop_xml = f"<w:rPr>{''.join(props)}</w:rPr>" if props else ""
    return f"<w:r>{prop_xml}<w:t{xml_space_attr(text)}>{esc(text)}</w:t></w:r>"


def m_text_run(text: str) -> str:
    return f"<m:r><m:t{xml_space_attr(text)}>{esc(text)}</m:t></m:r>"


def split_math_base(buffer: list[str]) -> str:
    text = "".join(buffer)
    if not text:
        return ""
    index = len(text)
    while index > 0:
        char = text[index - 1]
        if char.isspace() or char in "=+-*/(),[]{}<>≤≥≠≈∈∪∩":
            break
        index -= 1
    base = text[index:]
    del buffer[:]
    if text[:index]:
        buffer.append(text[:index])
    return base


def m_subscript(base: str, subscript: str) -> str:
    return (
        "<m:sSub>"
        "<m:sSubPr/>"
        f"<m:e>{m_run(base, convert_latex=False)}</m:e>"
        f"<m:sub>{m_run(subscript, convert_latex=False)}</m:sub>"
        "</m:sSub>"
    )


def find_subscript_close(text: str, start: int) -> int:
    depth = 1
    index = start
    while index < len(text):
        if text.startswith(SUBSCRIPT_OPEN, index):
            depth += 1
            index += len(SUBSCRIPT_OPEN)
            continue
        if text.startswith(SUBSCRIPT_CLOSE, index):
            depth -= 1
            if depth == 0:
                return index
            index += len(SUBSCRIPT_CLOSE)
            continue
        index += 1
    return -1


def m_run(text: str, *, convert_latex: bool = True) -> str:
    if convert_latex:
        text = latex_to_math_text(text)
    parts: list[str] = []
    buffer: list[str] = []
    index = 0
    while index < len(text):
        if text.startswith(SUBSCRIPT_OPEN, index):
            end = find_subscript_close(text, index + len(SUBSCRIPT_OPEN))
            if end == -1:
                buffer.append(text[index])
                index += 1
                continue
            subscript = text[index + len(SUBSCRIPT_OPEN):end]
            base = split_math_base(buffer)
            if not base:
                buffer.append("_")
                buffer.append(subscript)
            else:
                if buffer:
                    parts.append(m_text_run("".join(buffer)))
                    buffer = []
                parts.append(m_subscript(base, subscript))
            index = end + len(SUBSCRIPT_CLOSE)
            continue
        buffer.append(text[index])
        index += 1
    if buffer:
        parts.append(m_text_run("".join(buffer)))
    return "".join(parts)


def inline_math(math_text: str) -> str:
    return f"<m:oMath>{m_run(math_text)}</m:oMath>"


def block_math_para(math_text: str) -> str:
    paragraphs: list[str] = []
    display_lines = latex_to_display_lines(math_text)
    for index, line in enumerate(display_lines):
        before = "120" if index == 0 else "0"
        after = "120" if index == len(display_lines) - 1 else "0"
        paragraphs.append(
            "<w:p><w:pPr><w:jc w:val=\"center\"/>"
            f"<w:spacing w:before=\"{before}\" w:after=\"{after}\"/>"
            "</w:pPr>"
            f"<m:oMathPara><m:oMath>{m_run(line, convert_latex=False)}</m:oMath></m:oMathPara>"
            "</w:p>"
        )
    return "\n".join(paragraphs)


def parse_inline_runs(text: str) -> str:
    pieces: list[str] = []
    pos = 0
    pattern = re.compile(r"(`[^`]+`|\\\(.+?\\\))")
    for match in pattern.finditer(text):
        if match.start() > pos:
            pieces.append(w_run(text[pos:match.start()]))
        token = match.group(0)
        if token.startswith("`"):
            pieces.append(w_run(token[1:-1], style="CodeChar"))
        else:
            pieces.append(inline_math(token[2:-2]))
        pos = match.end()
    if pos < len(text):
        pieces.append(w_run(text[pos:]))
    return "".join(pieces)


def paragraph(text: str, style: str = "Normal", align: str | None = None) -> str:
    ppr = [f'<w:pStyle w:val="{style}"/>'] if style != "Normal" else []
    if align:
        ppr.append(f'<w:jc w:val="{align}"/>')
    ppr_xml = f"<w:pPr>{''.join(ppr)}</w:pPr>" if ppr else ""
    return f"<w:p>{ppr_xml}{parse_inline_runs(text)}</w:p>"


def empty_para() -> str:
    return "<w:p/>"


def page_break() -> str:
    return '<w:p><w:r><w:br w:type="page"/></w:r></w:p>'


def field_toc() -> str:
    return (
        "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>"
        f"{w_run('目录')}"
        "</w:p>"
        "<w:p>"
        "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r>"
        "<w:r><w:instrText xml:space=\"preserve\">TOC \\o \"1-3\" \\h \\z \\u</w:instrText></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
        f"{w_run('打开 Word 后右键此处，选择“更新域”，即可生成目录。')}"
        "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
        "</w:p>"
    )


def extract_svg_size(path: Path) -> tuple[int, int]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    width = re.search(r'width="([0-9.]+)', text)
    height = re.search(r'height="([0-9.]+)', text)
    if width and height:
        return int(float(width.group(1))), int(float(height.group(1)))
    viewbox = re.search(r'viewBox="[^"]*?([0-9.]+)\s+([0-9.]+)"', text)
    if viewbox:
        return int(float(viewbox.group(1))), int(float(viewbox.group(2)))
    return 1000, 700


def resolve_image_path(md_path: Path, image_path: str) -> Path:
    cleaned = image_path.replace("\\", "/")
    candidate = (md_path.parent / cleaned).resolve()
    if candidate.exists():
        return candidate
    return (ROOT / cleaned).resolve()


def make_image_para(image: ImageRel, doc_pr_id: int) -> str:
    return f"""
<w:p>
  <w:pPr><w:jc w:val="center"/></w:pPr>
  <w:r>
    <w:drawing>
      <wp:inline distT="0" distB="0" distL="0" distR="0">
        <wp:extent cx="{image.width_emu}" cy="{image.height_emu}"/>
        <wp:effectExtent l="0" t="0" r="0" b="0"/>
        <wp:docPr id="{doc_pr_id}" name="{esc(image.alt)}" descr="{esc(image.alt)}"/>
        <wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect="1"/></wp:cNvGraphicFramePr>
        <a:graphic>
          <a:graphicData uri="{NS_PIC}">
            <pic:pic>
              <pic:nvPicPr>
                <pic:cNvPr id="0" name="{esc(image.alt)}"/>
                <pic:cNvPicPr/>
              </pic:nvPicPr>
              <pic:blipFill>
                <a:blip r:embed="{image.rid}"/>
                <a:stretch><a:fillRect/></a:stretch>
              </pic:blipFill>
              <pic:spPr>
                <a:xfrm><a:off x="0" y="0"/><a:ext cx="{image.width_emu}" cy="{image.height_emu}"/></a:xfrm>
                <a:prstGeom prst="rect"><a:avLst/></a:prstGeom>
              </pic:spPr>
            </pic:pic>
          </a:graphicData>
        </a:graphic>
      </wp:inline>
    </w:drawing>
  </w:r>
</w:p>
""".strip()


def build_body_from_markdown(md_path: Path) -> tuple[list[str], list[ImageRel]]:
    text = md_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    body: list[str] = []
    images: list[ImageRel] = []
    paragraph_lines: list[str] = []
    in_code = False
    code_lines: list[str] = []
    in_math = False
    math_lines: list[str] = []
    first_title_seen = False
    image_count = 0

    def flush_paragraph() -> None:
        nonlocal paragraph_lines
        if paragraph_lines:
            body.append(paragraph(" ".join(line.strip() for line in paragraph_lines)))
            paragraph_lines = []

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("```"):
            flush_paragraph()
            if in_code:
                for code_line in code_lines:
                    body.append(paragraph(code_line if code_line else " ", style="CodeBlock"))
                code_lines = []
                in_code = False
            else:
                in_code = True
            continue

        if in_code:
            code_lines.append(line.rstrip("\n"))
            continue

        if stripped == r"\[":
            flush_paragraph()
            in_math = True
            math_lines = []
            continue
        if stripped == r"\]":
            body.append(block_math_para("\n".join(math_lines).strip()))
            in_math = False
            math_lines = []
            continue
        if in_math:
            math_lines.append(stripped)
            continue

        image_match = re.match(r"!\[(.*?)\]\((.*?)\)", stripped)
        if image_match:
            flush_paragraph()
            alt = image_match.group(1)
            image_path = resolve_image_path(md_path, image_match.group(2))
            if image_path.exists():
                image_count += 1
                rid = f"rIdImage{image_count}"
                width_px, height_px = extract_svg_size(image_path) if image_path.suffix.lower() == ".svg" else (1000, 700)
                width_emu = MAX_IMAGE_WIDTH_EMU
                height_emu = int(width_emu * height_px / max(width_px, 1))
                rel = ImageRel(
                    rid=rid,
                    docx_path=f"media/image{image_count}{image_path.suffix.lower()}",
                    source_path=image_path,
                    width_emu=width_emu,
                    height_emu=height_emu,
                    alt=alt,
                )
                images.append(rel)
                body.append(make_image_para(rel, image_count))
                body.append(paragraph(alt, style="Caption", align="center"))
            else:
                body.append(paragraph(f"{alt}（图片文件未找到：{image_match.group(2)}）", style="Caption", align="center"))
            continue

        if stripped.startswith("# "):
            flush_paragraph()
            if not first_title_seen:
                first_title_seen = True
                continue
            body.append(paragraph(stripped[2:], style="Heading1"))
            continue
        if stripped.startswith("## "):
            flush_paragraph()
            body.append(paragraph(stripped[3:], style="Heading1"))
            continue
        if stripped.startswith("### "):
            flush_paragraph()
            body.append(paragraph(stripped[4:], style="Heading2"))
            continue
        if stripped.startswith("#### "):
            flush_paragraph()
            body.append(paragraph(stripped[5:], style="Heading3"))
            continue

        if not stripped:
            flush_paragraph()
            continue

        paragraph_lines.append(line)

    flush_paragraph()
    return body, images


def document_xml(body_xml: Iterable[str]) -> str:
    body = "\n".join(body_xml)
    generated_date_text = f"生成日期：{GENERATED_DATE.year} 年 {GENERATED_DATE.month} 月 {GENERATED_DATE.day} 日"
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="{NS_W}" xmlns:r="{NS_R}" xmlns:wp="{NS_WP}" xmlns:a="{NS_A}" xmlns:pic="{NS_PIC}" xmlns:m="{NS_M}">
  <w:body>
    <w:p>
      <w:pPr><w:pStyle w:val="Title"/><w:jc w:val="center"/></w:pPr>
      {w_run('Classic 与 Data-Oriented ROAM 地形 LOD 实现与性能分析报告')}
    </w:p>
    <w:p>
      <w:pPr><w:pStyle w:val="Subtitle"/><w:jc w:val="center"/></w:pPr>
      {w_run('Classic ROAM 与 Data-Oriented ROAM 的实现和 Benchmark 对比')}
    </w:p>
    <w:p>
      <w:pPr><w:jc w:val="center"/><w:spacing w:before="720"/></w:pPr>
      {w_run('课程技术开发报告')}
    </w:p>
    <w:p>
      <w:pPr><w:jc w:val="center"/><w:spacing w:before="240"/></w:pPr>
      {w_run(generated_date_text)}
    </w:p>
    {page_break()}
    {field_toc()}
    {page_break()}
    {body}
    <w:sectPr>
      <w:headerReference w:type="default" r:id="rIdHeader1"/>
      <w:footerReference w:type="default" r:id="rIdFooter1"/>
      <w:pgSz w:w="11906" w:h="16838"/>
      <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/>
      <w:cols w:space="425"/>
      <w:docGrid w:type="lines" w:linePitch="312"/>
    </w:sectPr>
  </w:body>
</w:document>"""


def styles_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="{NS_W}">
  <w:docDefaults>
    <w:rPrDefault><w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="宋体" w:hAnsi="Times New Roman"/><w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr></w:rPrDefault>
    <w:pPrDefault><w:pPr><w:spacing w:line="360" w:lineRule="auto" w:after="120"/></w:pPr></w:pPrDefault>
  </w:docDefaults>
  <w:style w:type="paragraph" w:default="1" w:styleId="Normal">
    <w:name w:val="Normal"/>
    <w:pPr><w:spacing w:line="360" w:lineRule="auto" w:after="120"/><w:ind w:firstLine="480"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="宋体" w:hAnsi="Times New Roman"/><w:sz w:val="24"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Title">
    <w:name w:val="Title"/>
    <w:pPr><w:spacing w:before="2600" w:after="360"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="黑体" w:hAnsi="Times New Roman"/><w:b/><w:sz w:val="40"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Subtitle">
    <w:name w:val="Subtitle"/>
    <w:pPr><w:spacing w:after="360"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="宋体" w:hAnsi="Times New Roman"/><w:sz w:val="24"/><w:color w:val="666666"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Heading1">
    <w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/>
    <w:pPr><w:keepNext/><w:keepLines/><w:outlineLvl w:val="0"/><w:spacing w:before="480" w:after="240"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="黑体" w:hAnsi="Times New Roman"/><w:b/><w:sz w:val="32"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Heading2">
    <w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/>
    <w:pPr><w:keepNext/><w:keepLines/><w:outlineLvl w:val="1"/><w:spacing w:before="360" w:after="200"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="黑体" w:hAnsi="Times New Roman"/><w:b/><w:sz w:val="28"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Heading3">
    <w:name w:val="heading 3"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/>
    <w:pPr><w:keepNext/><w:keepLines/><w:outlineLvl w:val="2"/><w:spacing w:before="240" w:after="160"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="黑体" w:hAnsi="Times New Roman"/><w:b/><w:sz w:val="26"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Caption">
    <w:name w:val="Caption"/>
    <w:pPr><w:spacing w:before="80" w:after="180"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Times New Roman" w:eastAsia="宋体" w:hAnsi="Times New Roman"/><w:sz w:val="21"/><w:color w:val="555555"/></w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="CodeBlock">
    <w:name w:val="Code Block"/>
    <w:pPr><w:spacing w:line="260" w:lineRule="auto" w:before="40" w:after="40"/><w:ind w:left="420" w:firstLine="0"/><w:shd w:fill="F6F8FA"/></w:pPr>
    <w:rPr><w:rFonts w:ascii="Consolas" w:eastAsia="Consolas" w:hAnsi="Consolas"/><w:sz w:val="19"/></w:rPr>
  </w:style>
  <w:style w:type="character" w:styleId="CodeChar">
    <w:name w:val="Code Char"/>
    <w:rPr><w:rFonts w:ascii="Consolas" w:eastAsia="Consolas" w:hAnsi="Consolas"/><w:sz w:val="21"/><w:shd w:fill="F6F8FA"/></w:rPr>
  </w:style>
</w:styles>"""


def settings_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:settings xmlns:w="{NS_W}">
  <w:updateFields w:val="true"/>
  <w:defaultTabStop w:val="420"/>
  <w:characterSpacingControl w:val="doNotCompress"/>
</w:settings>"""


def header_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:hdr xmlns:w="{NS_W}" xmlns:r="{NS_R}">
  <w:p>
    <w:pPr><w:jc w:val="center"/><w:pBdr><w:bottom w:val="single" w:sz="4" w:space="1" w:color="999999"/></w:pBdr></w:pPr>
    {w_run('Classic 与 Data-Oriented ROAM 地形 LOD 实现与性能分析报告')}
  </w:p>
</w:hdr>"""


def footer_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:ftr xmlns:w="{NS_W}" xmlns:r="{NS_R}">
  <w:p>
    <w:pPr><w:jc w:val="center"/></w:pPr>
    {w_run('第 ')}
    <w:r><w:fldChar w:fldCharType="begin"/></w:r>
    <w:r><w:instrText xml:space="preserve">PAGE</w:instrText></w:r>
    <w:r><w:fldChar w:fldCharType="separate"/></w:r>
    {w_run('1')}
    <w:r><w:fldChar w:fldCharType="end"/></w:r>
    {w_run(' 页')}
  </w:p>
</w:ftr>"""


def document_rels_xml(images: list[ImageRel]) -> str:
    rels = [
        f'<Relationship Id="rIdStyles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>',
        f'<Relationship Id="rIdSettings" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings" Target="settings.xml"/>',
        f'<Relationship Id="rIdHeader1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/header" Target="header1.xml"/>',
        f'<Relationship Id="rIdFooter1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/>',
    ]
    for image in images:
        rels.append(
            f'<Relationship Id="{image.rid}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="{image.docx_path}"/>'
        )
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="{NS_REL}">
  {''.join(rels)}
</Relationships>"""


def root_rels_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="{NS_REL}">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>"""


def content_types_xml(images: list[ImageRel]) -> str:
    defaults = {
        "rels": "application/vnd.openxmlformats-package.relationships+xml",
        "xml": "application/xml",
    }
    for image in images:
        ext = image.source_path.suffix.lower().lstrip(".")
        if ext == "svg":
            defaults["svg"] = "image/svg+xml"
        elif ext == "png":
            defaults["png"] = "image/png"
        elif ext in {"jpg", "jpeg"}:
            defaults["jpeg"] = "image/jpeg"
    default_xml = "".join(
        f'<Default Extension="{ext}" ContentType="{ctype}"/>' for ext, ctype in defaults.items()
    )
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  {default_xml}
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
  <Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
  <Override PartName="/word/settings.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"/>
  <Override PartName="/word/header1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"/>
  <Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>"""


def core_xml() -> str:
    generated_timestamp = f"{GENERATED_DATE.isoformat()}T00:00:00Z"
    return f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:dcmitype="http://purl.org/dc/dcmitype/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <dc:title>Classic 与 Data-Oriented ROAM 地形 LOD 实现与性能分析报告</dc:title>
  <dc:creator>Codex</dc:creator>
  <cp:lastModifiedBy>Codex</cp:lastModifiedBy>
  <dcterms:created xsi:type="dcterms:W3CDTF">{generated_timestamp}</dcterms:created>
  <dcterms:modified xsi:type="dcterms:W3CDTF">{generated_timestamp}</dcterms:modified>
</cp:coreProperties>"""


def app_xml() -> str:
    return """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">
  <Application>Codex DOCX Builder</Application>
  <DocSecurity>0</DocSecurity>
  <ScaleCrop>false</ScaleCrop>
  <Company/>
  <LinksUpToDate>false</LinksUpToDate>
  <SharedDoc>false</SharedDoc>
  <HyperlinksChanged>false</HyperlinksChanged>
  <AppVersion>1.0</AppVersion>
</Properties>"""


def build_docx() -> None:
    body, images = build_body_from_markdown(REPORT_MD)
    OUTPUT_DOCX.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(OUTPUT_DOCX, "w", compression=zipfile.ZIP_DEFLATED) as docx:
        docx.writestr("[Content_Types].xml", content_types_xml(images))
        docx.writestr("_rels/.rels", root_rels_xml())
        docx.writestr("docProps/core.xml", core_xml())
        docx.writestr("docProps/app.xml", app_xml())
        docx.writestr("word/document.xml", document_xml(body))
        docx.writestr("word/styles.xml", styles_xml())
        docx.writestr("word/settings.xml", settings_xml())
        docx.writestr("word/header1.xml", header_xml())
        docx.writestr("word/footer1.xml", footer_xml())
        docx.writestr("word/_rels/document.xml.rels", document_rels_xml(images))
        for image in images:
            docx.write(image.source_path, f"word/{image.docx_path}")


if __name__ == "__main__":
    build_docx()
    print(OUTPUT_DOCX)
