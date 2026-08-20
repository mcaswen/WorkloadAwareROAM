$ErrorActionPreference = "Stop"

function Invoke-ParallelRoamPreset {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Preset,

        [string[]]$Arguments = @()
    )

    $cmakeCommand = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $cmakeCommand) {
        throw "CMake was not found in PATH. Install CMake 3.24 or newer and add its bin directory to PATH."
    }

    $projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

    Push-Location $projectRoot
    try {
        Write-Host "[ParallelROAM] configure: $Preset"
        & $cmakeCommand.Source --preset $Preset

        Write-Host "[ParallelROAM] build: $Preset"
        & $cmakeCommand.Source --build --preset $Preset --parallel

        $windowsExecutable = Join-Path $projectRoot "build/$Preset/bin/ParallelROAM.exe"
        $unixExecutable = Join-Path $projectRoot "build/$Preset/bin/ParallelROAM"

        if (Test-Path $windowsExecutable) {
            $executable = $windowsExecutable
        }
        else {
            $executable = $unixExecutable
        }

        if (-not (Test-Path $executable)) {
            throw "Executable not found: $executable"
        }

        Write-Host "[ParallelROAM] run: $executable $Arguments"
        & $executable @Arguments
    }
    finally {
        Pop-Location
    }
}
