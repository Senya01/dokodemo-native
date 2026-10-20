[CmdletBinding()]
param(
    [string]$OutputDirectory = 'build/functional',
    [string]$PreparedCodecRoot = '',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$root = Get-DokodemoRoot
$output = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else { [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory)) }
if ($output -eq $root) { throw 'Choose an out-of-source build directory.' }
[IO.Directory]::CreateDirectory($output) | Out-Null
$temporary = Join-Path $output 'tmp'
[IO.Directory]::CreateDirectory($temporary) | Out-Null
$env:TMP = $env:TEMP = $temporary
$toolchain = Get-DokodemoToolchain
$cmakeRoot = Join-Path $toolchain.installation_path 'Common7/IDE/CommonExtensions/Microsoft/CMake'
$cmake = Join-Path $cmakeRoot 'CMake/bin/cmake.exe'
$ctest = Join-Path $cmakeRoot 'CMake/bin/ctest.exe'
$ninja = Join-Path $cmakeRoot 'Ninja/ninja.exe'
$configure = @('-S', $root, '-B', $output, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    '-DDOKODEMO_ALLOW_EXTERNAL_BUILD=ON', '-DDOKODEMO_PACKAGE_ASSETS=OFF',
    '-DDOKODEMO_CORE_TESTS=OFF', '-DDOKODEMO_GAME_BOOT_TESTS=ON',
    '-DDOKODEMO_MODULE_CONTROLS_SOURCE=', '-DDOKODEMO_BACKEND_CONTROLS_SOURCE=',
    ('-DCMAKE_CXX_COMPILER=' + $toolchain.compiler),
    ('-DCMAKE_MAKE_PROGRAM=' + $ninja), ('-DDOKODEMO_FFMPEG_ROOT=' + $PreparedCodecRoot))
$null = Invoke-DokodemoProcess -FilePath $cmake -Arguments $configure -WorkingDirectory $root -LogPath (Join-Path $output 'configure.log')
$null = Invoke-DokodemoProcess -FilePath $cmake -Arguments @('--build', $output, '--target', 'dokodemo-native', 'dokodemo-game-boot-tests', '--parallel', '2') -WorkingDirectory $root -LogPath (Join-Path $output 'build.log')
if ($Test) {
    $null = Invoke-DokodemoProcess -FilePath $ctest -Arguments @('--test-dir', $output, '-R', '^game-boot-tests$', '--output-on-failure') -WorkingDirectory $root -LogPath (Join-Path $output 'test.log')
}
Write-Host "Native functional WIP built: $(Join-Path $output 'dokodemo-native.exe')"
