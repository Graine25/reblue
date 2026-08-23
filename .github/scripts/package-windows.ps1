# Stages the shipping file list into one zip. Both runtimes come out of the
# same build dir, so reblue.exe and reblue_vk.exe ship together.
#
# In: PRESET, OUT_FILE
$ErrorActionPreference = 'Stop'

$buildDir = "out/build/$env:PRESET"
$staging = Join-Path ([System.IO.Path]::GetTempPath()) "reblue-pkg"
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# The exes' own dependencies plus the Agility loader's D3D12\ copy, which is
# the only one plume's D3D12SDKPath ever reads. The debug layer, import libs
# and CMake junk stay out.
$files = @(
    'reblue.exe', 'reblue.pdb',
    'reblue_vk.exe', 'reblue_vk.pdb',
    'rexruntime.dll', 'dxcompiler.dll', 'dxil.dll',
    'D3D12\D3D12Core.dll',
    'gamecontrollerdb.txt'
)

foreach ($rel in $files) {
    $src = Join-Path $buildDir $rel
    if (-not (Test-Path $src)) { throw "missing required file: $src" }
    $dst = Join-Path $staging $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
    Copy-Item $src $dst -Force
}

New-Item -ItemType Directory -Force -Path dist | Out-Null
$zip = Join-Path "dist" $env:OUT_FILE
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -Recurse -Force $staging

"Packaged $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB) | Write-Host
