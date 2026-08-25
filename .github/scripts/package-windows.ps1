# Stages the shipping file list into one zip. Both runtimes come out of the
# same build dir, so reblue.exe (D3D12) and reblue_vk.exe ship together.
#
# In: PRESET, OUT_FILE
$ErrorActionPreference = 'Stop'

$buildDir = "out/build/$env:PRESET"
$staging = Join-Path ([System.IO.Path]::GetTempPath()) "reblue-pkg"
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# Written by CMake alongside the header the self-installer compiles in, so the
# shipped-file list has one owner. It sits outside the build's own output dir
# because it is not one of the files it names.
$listPath = Join-Path $buildDir 'packaging/program_files.txt'
if (-not (Test-Path $listPath)) { throw "missing $listPath - configure the build first" }
$files = @(Get-Content $listPath | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })

# Symbols ship so a playtester's crash log symbolizes.
$files += @($files | Where-Object { $_ -like '*.exe' } |
    ForEach-Object { [IO.Path]::ChangeExtension($_, '.pdb') })

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
