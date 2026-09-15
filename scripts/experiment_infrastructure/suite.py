"""展开有限实验清单；只准备输入，不启动性能矩阵。"""
from __future__ import annotations
from itertools import product
from pathlib import Path
from .catalog import load_json,resolve_case,content_hash
from .runner import save


def expand(base,output,budgets,workers,algorithms,prefixes):
    base=Path(base).resolve();output=Path(output).resolve()
    dimensions=[budgets,workers,algorithms,prefixes]
    if any(not values or len(values)!=len(set(values)) for values in dimensions):
        raise ValueError("维度必须非空且没有重复")
    combinations=list(product(*dimensions))
    if len(combinations)>128:raise ValueError("一次最多生成128个case，不自动扩大矩阵")
    original=load_json(base)
    resolve_case(base)
    output.mkdir(parents=True,exist_ok=False)
    entries=[]
    for budget,worker,algorithm,prefix in combinations:
        case={**original,"id":f'{original["id"]}-b{budget}-{algorithm}-t{worker}-{prefix}',
              "budget":budget,"workers":worker,"algorithm":algorithm,"prefix":prefix,
              "heightPolicy":original["heightPolicy"] if algorithm=="transactional" else "fit"}
        path=output/(case["id"]+".json");save(path,case)
        resolve_case(path)
        entries.append({"path":path.name,"sha256":content_hash(path)})
    result={"schemaVersion":"eip-suite-v1","base":str(base),"baseSha256":content_hash(base),
            "order":"budget, workers, algorithm, prefix; deterministic generation order, not randomized formal run order",
            "execution":"not started; each run requires explicit run command and unique output",
            "cases":entries}
    save(output/"suite.json",result)
    return output/"suite.json"
