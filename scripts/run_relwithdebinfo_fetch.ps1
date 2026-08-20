$ErrorActionPreference = "Stop"

. "$PSScriptRoot/common.ps1"

Invoke-ParallelRoamPreset -Preset "relwithdebinfo-fetch" -Arguments $args
