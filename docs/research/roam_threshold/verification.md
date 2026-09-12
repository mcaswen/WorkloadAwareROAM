# 形式化验证记录

> 日期：2026-09-12；检查时间：04:55:48 起，Asia/Shanghai。
> 对象：本目录 4 个 Lean 文件；不包含生产 C++ 的程序验证。

## 1. 实际结果

| 项目 | 结果 |
| --- | --- |
| `Closure.lean` | 内核检查成功，14 项定理/引理 |
| `Threshold.lean` | 内核检查成功，11 项定理/引理 |
| `Spectrum.lean` | 内核检查成功，3 项定理/引理 |
| `Counterexamples.lean` | 内核检查成功，8 项反例及其性质 |
| 全部 36 项声明的 `#print axioms` | 已执行；仅出现标准逻辑基础或无公理依赖 |
| `sorry` / `admit` / 自定义 `axiom` / `native_decide` / `unsafe` | 证明源文件中未出现 |
| 最终 4 文件检查及全声明公理审计耗时 | 2.603 秒，仅是本机证明工具运行记录 |

36 包含辅助引理和有限反例，不表示 36 项独立研究贡献。抽象闭包/层次命题有一般量化；Bool 反例使用可归约的有限判断，不冒充无限域普遍证明。

没有运行 ROAM 轨迹、性能复测或 CTest。本轮未修改生产代码、构建依赖和运行配置，证明工具时间也不代表候选算法执行成本。

## 2. 工具链与输入指纹

冻结工具链：`leanprover/lean4:v4.8.0`。本机输出为：

```text
Lean (version 4.8.0, x86_64-w64-windows-gnu, commit df668f00e6c0, Release)
```

来源：[官方发行页](https://github.com/leanprover/lean4/releases/tag/v4.8.0)，Windows 归档 `lean-4.8.0-windows.zip`。归档 SHA-256：

```text
1FCDF243D8F7E9CE36A491D9E7B122CA6526CDF34AFEB6F322E03653E865C224
```

工具仅解压至已忽略的 `tools/downloads/lean/lean-4.8.0-windows/`，未全局安装，也不进入 CMake。证明只使用随 Lean 发行的库。

| 源文件 | SHA-256 |
| --- | --- |
| `Closure.lean` | `A02B82457E338205B2D431A8117528BD8E7EA9A118ACB03749DF75481AFE2E8A` |
| `Threshold.lean` | `7872901BFF9BC307F94942E0FE6054271AC4701D3975C8F667C07BC50C7BC7D1` |
| `Spectrum.lean` | `F6C3713C16A985539DF53E1FCE5D8C90A86CA124D3FDD548FE22A6F595AAE471` |
| `Counterexamples.lean` | `E7425BE23852BF844CF60819677B498976ADB03A69B305C6CEC19052088EEE7A` |

## 3. 最小复现

在仓库根目录运行以下 PowerShell。先使用上述官方版本解压到本机约定位置；不要求安装 Mathlib、运行 Lake 或构建 C++。

```powershell
$ErrorActionPreference = 'Stop'
$proofDir = (Resolve-Path 'docs/research/roam_threshold/formal').Path
$leanExe = (Resolve-Path 'tools/downloads/lean/lean-4.8.0-windows/bin/lean.exe').Path
$env:LEAN_PATH = $proofDir
$modules = @('Closure', 'Threshold', 'Spectrum', 'Counterexamples')
foreach ($module in $modules) {
    & $leanExe -o (Join-Path $proofDir ($module + '.olean')) `
        (Join-Path $proofDir ($module + '.lean'))
    if ($LASTEXITCODE -ne 0) { throw ('Proof failed: ' + $module) }
}
```

源文件已打印核心命题的公理依赖。对全部声明进行同样审计，可继续运行：

```powershell
$auditDir = Join-Path (Get-Location).Path 'build/roam-threshold-proof'
New-Item -ItemType Directory -Path $auditDir -Force | Out-Null
$auditLines = [System.Collections.Generic.List[string]]::new()
foreach ($module in $modules) { $auditLines.Add('import ' + $module) }
$theoremCount = 0
foreach ($module in $modules) {
    $source = Get-Content -LiteralPath (Join-Path $proofDir ($module + '.lean')) -Raw -Encoding UTF8
    if ($source -match '\b(sorry|admit|axiom|native_decide|unsafe)\b') {
        throw ('Forbidden proof token: ' + $module)
    }
    $prefix = if ($module -eq 'Counterexamples') {
        'RoamThreshold.Counterexamples.'
    } else { 'RoamThreshold.' }
    foreach ($decl in [regex]::Matches($source, '(?m)^theorem\s+([A-Za-z_][A-Za-z_0-9]*)')) {
        $auditLines.Add('#print axioms ' + $prefix + $decl.Groups[1].Value)
        $theoremCount++
    }
}
$auditPath = Join-Path $auditDir 'Audit.lean'
[System.IO.File]::WriteAllLines($auditPath, $auditLines, [System.Text.UTF8Encoding]::new($false))
$auditOutput = & $leanExe $auditPath 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Axiom audit failed' }
if ($auditOutput.Count -ne $theoremCount) { throw 'Theorem count mismatch' }
foreach ($line in $auditOutput) {
    if ([string]$line -match 'depends on axioms:\s*\[(.*)\]') {
        foreach ($dependency in $Matches[1].Split(',')) {
            if ($dependency.Trim() -notin @('Classical.choice', 'propext', 'Quot.sound')) {
                throw ('Unexpected axiom: ' + $dependency)
            }
        }
    } elseif ([string]$line -notmatch 'does not depend on any axioms') {
        throw ('Unexpected output: ' + $line)
    }
}
$auditOutput
```

本轮实际日志在已忽略的 `build/roam-threshold-proof/proof-check.log`，审计输入为同目录 `Audit.lean`；文档保存稳定的结果、命令和源文件指纹，不依赖提交本地日志。`formal/.gitignore` 忽略 `*.olean`、`*.ilean` 和 `.lake/`。

## 4. 完整声明索引

| 文件 | 声明 |
| --- | --- |
| [Closure.lean](formal/Closure.lean) | `reach_trans`、`closed_reach`、`closure_extensive`、`closure_monotone`、`closure_closed`、`closure_least`、`closure_idempotent`、`closure_union`、`closure_intersection_subset`、`relative_extensive`、`relative_monotone`、`relative_union`、`relative_idempotent`、`relative_matches_initial_union` |
| [Threshold.lean](formal/Threshold.lean) | `required_necessary`、`required_sufficient`、`fits_iff_required`、`target_least`、`target_admissible`、`target_monotone`、`forbidden_target_impossible`、`weight_sum_monotone`、`weight_sum_union_intersection`、`closure_cost_submodular`、`hard_budget_iff` |
| [Spectrum.lean](formal/Spectrum.lean) | `support_maximum_spec`、`support_equals_closure`、`target_activation` |
| [Counterexamples.lean](formal/Counterexamples.lean) | `horn_extensive`、`horn_monotone`、`horn_idempotent`、`horn_union_fails`、`maximum_error_gain_not_additive`、`nonmonotone_hidden_child`、`certificate_failure_not_actual_infeasibility`、`penalty_misses_budget_optimum` |

## 5. 可信边界与非空洞性审计

`#print axioms` 的全集为 `Classical.choice`、`propext`、`Quot.sound`，部分命题没有公理依赖。这些是 Lean 标准逻辑基础，含义见[官方说明](https://lean-lang.org/theorem_proving_in_lean4/Axioms-and-Computation/)。没有 `sorryAx`、原生外部计算断言或项目自定义公理。工具二进制、内核和标准基础属于可信计算基础。

检查证明内容时，特别核对了以下边界：

1. `Hierarchy` 的深度下降和优先级单调是显式假设；`Fits ↔ Required⊆I` 是基于这些假设证明的结论，没有把它写成结构字段。
2. `Closed edge I` 是抽象可行集定义。它与真实无裂缝 ROAM 网格的对应只有纸面论证；Lean 没有坐标、两子节点划分、底边伙伴和邻接数组对象。
3. `WeightSum` 对任意有限列表都定义一个代价函数。`hard_budget_iff` 对此函数成立，但要称为实际三角形数，仍须完整无重复事件枚举与合法叶数公式。不能把“代价函数证明了”替换成“实际叶数计数已验证”。
4. `target_activation` 明确要求完整枚举和初始集闭合；没有偷偷把当前 split queue 当作完整潜在层次。
5. 禁止终端不是默认满足的前提。`forbidden_target_impossible` 证明一旦最小闭包触及它们，就不存在满足模型的目标。
6. 几何连续上界、实数优先级到有限序数的映射、diamond 收缩算法、并行调度和 work/span 尚未形式化。`noncomputable` 数学定义也不是机器生成的可执行 CPU 算法。

因此可报告“抽象阈值闭包模型的证明已由 Lean 内核检查”；不能报告“ROAM 拓扑更新、质量保证及并行实现已经形式化验证”。
