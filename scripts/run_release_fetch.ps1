$ErrorActionPreference = "Stop"

. "$PSScriptRoot/common.ps1"

Invoke-ParallelRoamPreset -Preset "release-fetch" -Arguments $args
