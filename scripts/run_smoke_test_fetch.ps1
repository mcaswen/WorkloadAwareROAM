$ErrorActionPreference = "Stop"

. "$PSScriptRoot/common.ps1"

$scriptArguments = @("--smoke-test") + $args
Invoke-ParallelRoamPreset -Preset "debug-fetch" -Arguments $scriptArguments
