"""核对离线报告引用和绘图来源；视觉检查必须另外实际观看。"""
from __future__ import annotations
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit,unquote
from .catalog import load_json,content_hash


class References(HTMLParser):
    def __init__(self):
        super().__init__();self.links=[];self.ids=set()
    def handle_starttag(self,tag,attrs):
        values=dict(attrs)
        if "id" in values:
            if values["id"] in self.ids: raise ValueError("重复HTML id")
            self.ids.add(values["id"])
        for key in ("href","src"):
            if key in values:self.links.append(values[key])


def check(report):
    root=Path(report).resolve();parser=References()
    parser.feed((root/"index.html").read_text(encoding="utf-8"))
    missing=[]
    for link in parser.links:
        url=urlsplit(link)
        if url.scheme or url.netloc:continue
        if not url.path:
            if url.fragment and unquote(url.fragment) not in parser.ids:missing.append(link)
        elif not (root/unquote(url.path)).is_file():missing.append(link)
    if missing:raise ValueError("报告链接缺失："+repr(missing))
    count=0
    for directory in ("figures","visual-figures"):
        for path in (root/directory).glob("*.spec.json"):
            spec=load_json(path)
            if spec["analysisSha256"]!=content_hash(root/"analysis.json"):raise ValueError("图表来源不一致")
            for entry in spec["files"].values():
                if content_hash(path.parent/entry["path"])!=entry["sha256"]:raise ValueError("图表内容改变")
            count+=1
    return {"localLinks":len(parser.links),"figureSpecs":count,"status":"pass",
            "visualReview":"requires actual image/page inspection"}
