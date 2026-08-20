param()

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Measure-CommentCoverage([string]$relativePath)
{
    $fullPath = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath))
    {
        throw "Comment coverage path is missing: $relativePath"
    }

    $files = if (Test-Path -LiteralPath $fullPath -PathType Container)
    {
        Get-ChildItem -LiteralPath $fullPath -Recurse -File -Include *.h,*.cpp
    }
    else
    {
        Get-Item -LiteralPath $fullPath
    }

    $codeLines = 0
    $commentLines = 0
    foreach ($file in $files)
    {
        $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
        foreach ($line in $lines)
        {
            $trimmed = $line.Trim()
            if ($trimmed.Length -eq 0)
            {
                continue
            }

            if ($line -match '//|/\*|\*/')
            {
                $commentLines += 1
            }

            # 纯结构和预处理行不承载业务逻辑，否则会鼓励给字段和样板赋值补翻译注释
            if ($trimmed -match '^(//|/\*|\*|\*/)' -or
                $trimmed -match '^#' -or
                $trimmed -match '^[{}]+;?$' -or
                $trimmed -match '^(public|private|protected):$' -or
                $trimmed -match '^namespace(\s|$)')
            {
                continue
            }

            $codeLines += 1
        }
    }

    $rate = if ($codeLines -gt 0) { 100.0 * $commentLines / $codeLines } else { 0.0 }
    return [pscustomobject]@{
        Path = $relativePath
        CodeLines = $codeLines
        CommentLines = $commentLines
        Rate = $rate
    }
}

$coverageRequirements = @(
    [pscustomobject]@{ Path = "src"; MinimumPercent = 15.0 },
    [pscustomobject]@{ Path = "src/render"; MinimumPercent = 12.0 },
    [pscustomobject]@{ Path = "src/algorithms/data_oriented_roam"; MinimumPercent = 20.0 },
    [pscustomobject]@{ Path = "src/algorithms/classic_roam"; MinimumPercent = 20.0 },
    [pscustomobject]@{ Path = "src/platform"; MinimumPercent = 12.0 }
)

$failed = $false
foreach ($requirement in $coverageRequirements)
{
    $coverage = Measure-CommentCoverage $requirement.Path
    Write-Host ("{0}: {1}/{2} = {3:N1}% (required {4:N1}%)" -f `
        $coverage.Path,
        $coverage.CommentLines,
        $coverage.CodeLines,
        $coverage.Rate,
        $requirement.MinimumPercent)
    if ($coverage.Rate + 0.0001 -lt $requirement.MinimumPercent)
    {
        $failed = $true
    }
}

if ($failed)
{
    Write-Error "C++ comment coverage does not meet the project and complex-module requirements."
    exit 1
}
