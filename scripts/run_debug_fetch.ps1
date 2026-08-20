$ErrorActionPreference = "Stop"

. "$PSScriptRoot/common.ps1"

Invoke-ParallelRoamPreset -Preset "debug-fetch" -Arguments $args
