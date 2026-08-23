# Imports the Visual Studio developer environment into the job. The presets
# drive clang through Ninja, which has no developer environment of its own.
#
# In: GITHUB_ENV
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if (-not $install) { throw "no Visual Studio install carrying the x64 C++ toolset" }

Import-Module (Join-Path $install 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $install -SkipAutomaticLocation `
  -DevCmdArguments '-arch=x64 -host_arch=x64'

foreach ($item in Get-ChildItem env:) {
  if ($item.Name -match '^(GITHUB_|RUNNER_|ACTIONS_)') { continue }
  if ($item.Value -match '[\r\n]') { continue }
  "$($item.Name)=$($item.Value)" | Out-File $env:GITHUB_ENV -Encoding utf8 -Append
}

Write-Host "Visual Studio: $install"
Write-Host "WindowsSdkDir: $env:WindowsSdkDir $env:WindowsSDKVersion"
