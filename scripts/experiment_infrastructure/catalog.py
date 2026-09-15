"""校验实际资产和版本化配置；所有内容检查仅在启动/离线阶段发生。"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "assets/experiments/terrain_catalog.json"


def load_json(path: Path) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"重复字段: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique,
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))


def content_hash(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def source_samples(path: Path) -> np.ndarray:
    """统一为stb_load_16使用的灰度U16范围，不经色彩转换或gamma。"""
    with Image.open(path) as image:
        if image.mode not in ("L", "I", "I;16", "I;16B", "I;16L"):
            raise ValueError(f"高度输入必须是灰度: {image.mode}")
        values = np.asarray(image)
        if image.mode == "L":
            values = values.astype(np.uint16) * 257
        if values.min() < 0 or values.max() > 65535:
            raise ValueError("高度输入超出U16")
        return np.asarray(values, dtype="<u2").copy()


def sample_hash(values: np.ndarray) -> str:
    return hashlib.sha256(values.astype("<u2").tobytes(order="C")).hexdigest()


def local_path(root: Path, value: str) -> Path:
    result = (root / value).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError(f"路径超出输入根: {value}")
    return result


def validate_catalog(path: Path = CATALOG, root: Path = ROOT) -> list[dict]:
    import jsonschema
    data = load_json(path)
    jsonschema.validate(data, load_json(root / "configs/experiments/schema/terrain_catalog.schema.json"))
    if data.get("schemaVersion") != 1:
        raise ValueError("不支持资产目录版本")
    entries = data["terrains"]
    seen = set()
    for asset in entries:
        if asset["id"] in seen:
            raise ValueError("资产ID重复")
        seen.add(asset["id"])
        source = local_path(root, asset["path"])
        if content_hash(source) != asset["fileSha256"]:
            raise ValueError(f"文件内容不符: {asset['id']}")
        values = source_samples(source)
        if list(values.shape) != [asset["height"], asset["width"]]:
            raise ValueError("真实尺寸与目录不符")
        if sample_hash(values) != asset["sampleSha256"]:
            raise ValueError("样本内容不符")
        for field in ("terrainSize", "heightScale"):
            number = asset[field]
            if not math.isfinite(number) or number <= 0:
                raise ValueError(f"无效尺度: {field}")
        if not asset.get("provenance") or "licenseStatus" not in asset["provenance"]:
            raise ValueError("来源/许可状态缺失")
    return entries


def resolve_case(path: Path, root: Path = ROOT) -> dict:
    """显式展开资产默认值，生成C++消费的独立配置；不创建运行状态。"""
    import jsonschema
    case = load_json(path)
    schema = load_json(root / "configs/experiments/schema/case.schema.json")
    jsonschema.validate(case, schema)
    assets = {item["id"]: item for item in validate_catalog(root / "assets/experiments/terrain_catalog.json", root)}
    asset = assets[case["terrain"]]
    result = dict(case)
    result.update(heightMap=str(local_path(root, asset["path"])),
                  fileSha256=asset["fileSha256"], sampleSha256=asset["sampleSha256"],
                  width=asset["width"], height=asset["height"],
                  terrainSize=asset["terrainSize"], heightScale=asset["heightScale"])
    cameras = load_json(root / "configs/experiments/cameras/catalog.json")["cameras"]
    camera_id = case["camera"] if case["camera"].startswith(case["terrain"]+"-") else case["terrain"]+"-"+case["camera"]
    camera = next((item for item in cameras if item["id"] == camera_id), None)
    if camera is None or camera["asset"] != case["terrain"]:
        raise ValueError("路线未冻结或资产不匹配")
    camera_file = local_path(root, camera["path"])
    if content_hash(camera_file) != camera["sha256"]:
        raise ValueError("冻结相机文件被修改")
    materials = load_json(root / "assets/experiments/material_catalog.json")["materials"]
    material = next((item for item in materials if item["id"] == case["material"]), None)
    if material is None:
        raise ValueError("未知材质")
    material_file = local_path(root, material["path"])
    if content_hash(material_file) != material["sha256"]:
        raise ValueError("材质内容不符")
    if case["algorithm"] != "transactional" and case["heightPolicy"] != "fit":
        raise ValueError("高度策略仅适用于Transactional")
    if case.get("flipRecovery", False) and (case["algorithm"] != "transactional" or case["heightPolicy"] != "immutable"):
        raise ValueError("翻边恢复要求Transactional固定旧点策略")
    result["flipRecovery"] = case.get("flipRecovery", False)
    result.update(cameraFile=str(camera_file), cameraSha256=camera["sha256"],
        cameraFnv64=fnv64(camera_file.read_bytes()), sampleFnv64=fnv64(source_samples(local_path(root,asset["path"])).tobytes()),
        materialFile=str(material_file), materialSha256=material["sha256"],
        materialTiling=material["tiling"], materialTint=material["heightTint"],
        mergePixels=case.get("mergePixels",case["splitPixels"]*.5),
        captureStride=case.get("captureStride",4), warmup=case.get("warmup",3),
        maxFrames=case.get("maxFrames",10000))
    if result["mergePixels"] > result["splitPixels"]:
        raise ValueError("merge阈值不能大于split阈值")
    return result


def fnv64(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64)-1)
    return str(value)
