param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$projectRoot = Split-Path -Parent $PSScriptRoot
$downloadRoot = Join-Path $projectRoot "tools\downloads"
$agilityRoot = Join-Path $projectRoot "third_party\microsoft\d3d12-agility-sdk\1.614.1"
$dxcRoot = Join-Path $projectRoot "third_party\microsoft\dxc\1.7.2308.12"

function Install-NuGetPackage
{
    param(
        [string]$Name,
        [string]$Url,
        [string]$PackagePath,
        [string]$PackageSha256,
        [string]$Destination,
        [string]$ValidationFile,
        [string]$ValidationSha256
    )

    $installedFile = Join-Path $Destination $ValidationFile
    if (-not $Force -and (Test-Path $installedFile))
    {
        $installedHash = (Get-FileHash $installedFile -Algorithm SHA256).Hash
        if ($installedHash -eq $ValidationSha256)
        {
            Write-Host "$Name is already installed"
            return
        }
    }

    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
    if ($Force -or -not (Test-Path $PackagePath) -or
        (Get-FileHash $PackagePath -Algorithm SHA256).Hash -ne $PackageSha256)
    {
        $temporaryPackage = "$PackagePath.download"
        Invoke-WebRequest -Uri $Url -OutFile $temporaryPackage
        $downloadedHash = (Get-FileHash $temporaryPackage -Algorithm SHA256).Hash
        if ($downloadedHash -ne $PackageSha256)
        {
            Remove-Item -LiteralPath $temporaryPackage -Force
            throw "$Name package hash mismatch"
        }
        Move-Item -LiteralPath $temporaryPackage -Destination $PackagePath -Force
    }

    $stagingDirectory = "$Destination.staging"
    Remove-Item -LiteralPath $stagingDirectory -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($PackagePath, $stagingDirectory)

    $stagedFile = Join-Path $stagingDirectory $ValidationFile
    if (-not (Test-Path $stagedFile) -or
        (Get-FileHash $stagedFile -Algorithm SHA256).Hash -ne $ValidationSha256)
    {
        throw "$Name extracted file hash mismatch"
    }

    Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath $stagingDirectory -Destination $Destination
    Write-Host "$Name installed"
}

Install-NuGetPackage `
    -Name "D3D12 Agility SDK 1.614.1" `
    -Url "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.614.1" `
    -PackagePath (Join-Path $downloadRoot "microsoft.direct3d.d3d12.1.614.1.nupkg") `
    -PackageSha256 "9880AA91602DD51DD6CF7911A2BCA7A2323513B15338573CDE014B3356EEAFF2" `
    -Destination $agilityRoot `
    -ValidationFile "build\native\bin\x64\D3D12Core.dll" `
    -ValidationSha256 "8A23D826B25B4329522FF451CB52B7F2B34D7F2913CFEB878371CE8BD765FE2D"

Install-NuGetPackage `
    -Name "DirectX Shader Compiler 1.7.2308.12" `
    -Url "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC/1.7.2308.12" `
    -PackagePath (Join-Path $downloadRoot "microsoft.direct3d.dxc.1.7.2308.12.nupkg") `
    -PackageSha256 "CB8A225CEDF9C092A25924925196A02D11D5B850252EB163D6CA7150DEE09836" `
    -Destination $dxcRoot `
    -ValidationFile "build\native\bin\x64\dxc.exe" `
    -ValidationSha256 "1C9E7CB6C9FB8593E9253FF7FCAE998D2E23A9730722D44229A56497A0D366E7"
