"""获取官方3DEP原始浮点高程导出，保存转换证据后量化为运行输入。"""
from __future__ import annotations
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path
import numpy as np
from PIL import Image
from .catalog import ROOT, CATALOG, load_json, content_hash, sample_hash

SERVICE = "https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer"
PROJECT = "https://utility.arcgisonline.com/ArcGIS/rest/services/Geometry/GeometryServer/project"
REGIONS = [("sierra", "Sierra Nevada山地", 37.65, -119.65, 32611),
           ("canyon", "Colorado河谷", 36.10, -112.10, 32612),
           ("hills", "Appalachian缓丘", 36.20, -81.70, 32617),
           ("lowland", "沿海低地", 35.00, -76.95, 32618)]


def download(url: str, destination: Path):
    if destination.exists():
        return
    # 绕过当前不可达的WSL localhost代理，只影响本次公开资产请求
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(url, timeout=90) as response:
        content = response.read(8*1024*1024+1)
    if len(content) > 8*1024*1024:
        raise ValueError("导出响应超过8MiB")
    destination.write_bytes(content)


def query(url: str, params: dict, destination: Path):
    full = url + "?" + urllib.parse.urlencode(params)
    download(full, destination)
    data = load_json(destination)
    if "error" in data:
        raise ValueError(data["error"])
    return data, full


def import_regions():
    raw = ROOT / "benchmark-output/experiment-infrastructure/eip-02/dem-source"
    raw.mkdir(parents=True, exist_ok=True)
    service, _ = query(SERVICE, {"f":"json"}, raw / "service.json")
    catalog = load_json(CATALOG)
    known = {asset["id"]:asset for asset in catalog["terrains"]}
    target = ROOT / "assets/heightmaps/experiment/imported"
    target.mkdir(parents=True, exist_ok=True)
    for ident, name, lat, lon, sr in REGIONS:
        start = time.perf_counter()
        projected, projection_url = query(PROJECT, dict(f="json", inSR=4326, outSR=sr,
            geometries=json.dumps({"geometryType":"esriGeometryPoint",
                                  "geometries":[{"x":lon,"y":lat}]})), raw / (ident+"-center.json"))
        center = projected["geometries"][0]
        # bbox描述像素外边界，额外半格让首末采样中心正好相距10km
        radius = 5000 * 513 / 512
        bounds = [center["x"]-radius,center["y"]-radius,center["x"]+radius,center["y"]+radius]
        params = dict(f="json",bbox=",".join(format(x,".15g") for x in bounds),bboxSR=sr,
                      imageSR=sr,size="513,513",format="tiff",pixelType="F32",
                      interpolation="+RSP_BilinearInterpolation".lstrip("+"),
                      renderingRule=json.dumps({"rasterFunction":"None"}),adjustAspectRatio="false")
        exported, url = query(SERVICE+"/exportImage", params, raw / (ident+"-export.json"))
        tif = raw / (ident+".tif")
        download(exported["href"], tif)
        with Image.open(tif) as image:
            if image.mode != "F":
                raise ValueError("服务未提供原始F32高程")
            # TIFF北向上；运行row=v向北，故只在转换阶段翻转一次
            elevations = np.asarray(image,dtype=np.float64)[::-1].copy()
            nodata = image.tag_v2.get(42113)
        if elevations.shape != (513,513) or not np.isfinite(elevations).all():
            raise ValueError("尺寸或缺失高程异常")
        if nodata is not None and np.any(elevations == float(str(nodata).strip("\x00"))):
            raise ValueError("裁剪含NoData，禁止填零")
        if elevations.min() < -500 or elevations.max() > 9000:
            raise ValueError("高程范围异常")
        minimum, maximum = float(elevations.min()), float(elevations.max())
        span = maximum-minimum
        if span <= 0:
            raise ValueError("自然高程范围退化")
        samples = np.rint((elevations-minimum)*65535/span).astype("<u2")
        extent = exported["extent"]
        spacing = (extent["xmax"]-extent["xmin"])/513
        actual_span = spacing*512
        if abs(actual_span-10000)>0.01 or abs((extent["ymax"]-extent["ymin"])/513-spacing)>0.0001:
            raise ValueError("服务改变冻结方形裁剪")
        path = target / f"Hm_Experiment_{ident.title()}_513.png"
        if path.exists():
            from .catalog import source_samples
            assert np.array_equal(source_samples(path),samples), "已有资产不能静默替换"
        else:
            Image.fromarray(samples).save(path)
        known["dem-"+ident] = dict(id="dem-"+ident,name=name,path=path.relative_to(ROOT).as_posix(),
            width=513,height=513,terrainSize=80,heightScale=span*80/actual_span,
            kind="imported",fileSha256=content_hash(path),sampleSha256=sample_hash(samples),
            sampleEncoding="uint16-le-row-major",
            provenance=dict(source="USGS 3DEP Bare Earth DEM export; frozen downloaded F32 raster",
                licenseStatus="USGS 3DEP public domain",
                licenseUrl="https://www.usgs.gov/3dep-product-news",
                acquiredAt="2026-09-15",sourceSha256=content_hash(tif),sourceFile=tif.relative_to(ROOT).as_posix(),
                serviceDescription=service["serviceDescription"],exportUrl=url,projectionUrl=projection_url,
                exportMetadata=exported,centerWgs84=[lon,lat],epsg=sr,
                elevationOffsetMeters=minimum,elevationRangeMeters=span,
                intervalSpanMeters=actual_span,verticalExaggeration=1,
                noDataCount=0,rounding="nearest-even",rowMapping="north-up TIFF flipped; row=v increases north",
                reference="quantized U16 + frozen bilinear interpolation",
                sourceLimitation="服务采用多分辨率镶嵌及默认datum转换；保存实际F32导出作为冻结来源，不声称原始测量点",
                importSeconds=time.perf_counter()-start))
        catalog["terrains"] = list(known.values())
        CATALOG.write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(ident,minimum,maximum,known["dem-"+ident]["heightScale"],flush=True)


if __name__ == "__main__":
    import_regions()
