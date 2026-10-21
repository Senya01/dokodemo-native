[CmdletBinding()]
param(
    [string]$OutputDirectory = 'build/functional',
    [string]$PreparedCodecRoot = '',
    [string]$UserTablesDirectory = '',
    [string]$PrivateSourceFixturesDirectory = '',
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
    ('-DCMAKE_MAKE_PROGRAM=' + $ninja), ('-DDOKODEMO_FFMPEG_ROOT=' + $PreparedCodecRoot),
    ('-DDOKODEMO_USER_TABLES_DIR=' + $UserTablesDirectory),
    ('-DDOKODEMO_PRIVATE_SOURCE_FIXTURES_DIR=' + $PrivateSourceFixturesDirectory))
$null = Invoke-DokodemoProcess -FilePath $cmake -Arguments $configure -WorkingDirectory $root -LogPath (Join-Path $output 'configure.log')
$null = Invoke-DokodemoProcess -FilePath $cmake -Arguments @('--build', $output, '--target', 'dokodemo-native', 'dokodemo-game-boot-tests', 'dokodemo-game-graphics-tests', 'dokodemo-game-filesystem-tests', 'dokodemo-game-content-tests', 'dokodemo-game-scene-ui-tests', 'dokodemo-game-storage-tests', 'dokodemo-game-model-tests', 'dokodemo-game-text-tests', 'dokodemo-game-actor-tests', 'dokodemo-game-sound-tests', '--parallel', '2') -WorkingDirectory $root -LogPath (Join-Path $output 'build.log')
if ($Test) {
    $null = Invoke-DokodemoProcess -FilePath $ctest -Arguments @('--test-dir', $output, '-R', '^game-(boot|graphics|filesystem|content|scene-ui|storage|model|text|actor|sound)-tests$', '--output-on-failure') -WorkingDirectory $root -LogPath (Join-Path $output 'test.log')
}
Write-Host "Native functional WIP built: $(Join-Path $output 'dokodemo-native.exe')"
