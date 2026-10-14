# PowerShell 5.1+ helpers shared by the native build and asset preparation.
Set-StrictMode -Version Latest

function Get-DokodemoRoot {
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}

function Resolve-DokodemoPath {
    param([string]$Root, [string]$Path, [switch]$MustExist)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'An empty path is not allowed.' }
    $candidate = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $Root $Path }
    $resolved = [IO.Path]::GetFullPath($candidate)
    $prefix = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path must stay inside the workspace: $Path"
    }
    if ($MustExist -and -not (Test-Path -LiteralPath $resolved)) { throw "Missing input: $resolved" }
    # Do not let directory junctions escape the workspace or its ownership boundaries.
    $ancestor = $resolved
    while ($ancestor -and $ancestor.Length -ge $Root.Length) {
        if (Test-Path -LiteralPath $ancestor) {
            if (((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse-point paths are not supported: $ancestor"
            }
        }
        if ($ancestor.Equals($Root, [StringComparison]::OrdinalIgnoreCase)) { break }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $resolved
}

function Get-DokodemoRelativePath {
    param([string]$Root, [string]$Path)
    $absolute = [IO.Path]::GetFullPath($Path)
    $prefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $absolute.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Path must stay inside the workspace: $Path" }
    return $absolute.Substring($prefix.Length).Replace('\', '/')
}

function Get-DokodemoSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-DokodemoUtf8 {
    param([string]$Path, [string]$Text)
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

function Write-DokodemoJson {
    param([string]$Path, $Value)
    Write-DokodemoUtf8 -Path $Path -Text (($Value | ConvertTo-Json -Depth 20) + "`n")
}

function Get-DokodemoInventory {
    param([string]$Root, [string[]]$Paths)
    $files = @{}
    foreach ($path in $Paths) {
        $absolute = Resolve-DokodemoPath -Root $Root -Path $path
        if (-not (Test-Path -LiteralPath $absolute)) { continue }
        $item = Get-Item -LiteralPath $absolute
        $items = if ($item.PSIsContainer) { @(Get-ChildItem -LiteralPath $absolute -Recurse -Force) } else { @($item) }
        foreach ($file in $items) {
            if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse-point inputs are not supported: $($file.FullName)" }
            if ($file.PSIsContainer) { continue }
            $relative = Get-DokodemoRelativePath -Root $Root -Path $file.FullName
            if ($relative -match '(^|/)(__pycache__|\.git)(/|$)' -or $relative -match '\.pyc$') { continue }
            $files[$relative] = $file
        }
    }
    $names = [string[]]@($files.Keys)
    [Array]::Sort($names, [StringComparer]::Ordinal)
    foreach ($name in $names) {
        $file = $files[$name]
        [ordered]@{ path = $name; size_bytes = [long]$file.Length; sha256 = Get-DokodemoSha256 $file.FullName }
    }
}

function ConvertTo-DokodemoNativeArgument {
    param([AllowEmptyString()][string]$Value)
    # Windows CRT quoting: double backslashes before quotes and the closing quote.
    if ($Value -match '^[^\s"]+$') { return $Value }
    return '"' + ([regex]::Replace(([regex]::Replace($Value, '(\\*)"', '$1$1\"')), '(\\+)$', '$1$1')) + '"'
}

function Invoke-DokodemoProcess {
    param(
        [string]$FilePath, [string[]]$Arguments = @(), [string]$WorkingDirectory,
        [string]$LogPath, [int]$TimeoutSeconds = 0, [switch]$Quiet, [string]$RawArguments, [switch]$Utf8Output
    )
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $FilePath
    $info.Arguments = if ($RawArguments) { $RawArguments } else { (($Arguments | ForEach-Object { ConvertTo-DokodemoNativeArgument $_ }) -join ' ') }
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    if ($Utf8Output) {
        $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
        $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw "Could not start $FilePath" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $finished = if ($TimeoutSeconds -gt 0) { $process.WaitForExit($TimeoutSeconds * 1000) } else { $process.WaitForExit(); $true }
        if (-not $finished) {
            $process.Kill()
            $process.WaitForExit()
        }
        $out = $stdout.GetAwaiter().GetResult()
        $err = $stderr.GetAwaiter().GetResult()
        if ($LogPath) { Write-DokodemoUtf8 -Path $LogPath -Text ($out + $err) }
        if (-not $Quiet) {
            $display = ($out + $err).TrimEnd()
            $lines = @($display -split '\r?\n')
            if ($lines.Count -gt 80) {
                Write-Host (($lines | Select-Object -First 10) -join [Environment]::NewLine)
                Write-Host "... $($lines.Count - 30) lines omitted; full output: $LogPath ..."
                Write-Host (($lines | Select-Object -Last 20) -join [Environment]::NewLine)
            } elseif ($display) { Write-Host $display }
        }
        if (-not $finished) { throw "Process exceeded ${TimeoutSeconds}s: $FilePath (log: $LogPath)" }
        if ($process.ExitCode -ne 0) { throw "Process exited with code $($process.ExitCode): $FilePath (log: $LogPath)" }
        return [pscustomobject]@{ exit_code = $process.ExitCode; stdout = $out; stderr = $err }
    } finally {
        $process.Dispose()
    }
}

function Get-DokodemoToolchain {
    param([string]$VsInstallPath)
    if (-not $VsInstallPath) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe was not found. Install the MSVC 2022 x64 C++ tools, or pass -VsInstallPath.' }
        $found = Invoke-DokodemoProcess -FilePath $vswhere -Arguments @('-latest', '-products', '*', '-version', '[17.0,18.0)', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json') -WorkingDirectory (Get-DokodemoRoot) -Quiet
        $instances = @($found.stdout | ConvertFrom-Json)
        if ($instances.Count -eq 0) { throw 'No Visual Studio 2022 installation with x64 C++ tools was found.' }
        $VsInstallPath = $instances[0].installationPath
        $vsVersion = $instances[0].installationVersion
    } else {
        $vsVersion = 'explicit-install-path'
    }
    $VsInstallPath = [IO.Path]::GetFullPath($VsInstallPath)
    $vcvars = Join-Path $VsInstallPath 'VC/Auxiliary/Build/vcvarsall.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "Missing MSVC environment script: $vcvars" }
    if ($vcvars -match '["%\r\n]') { throw 'Unsupported characters in the Visual Studio installation path.' }
    $cmd = Join-Path $env:SystemRoot 'System32/cmd.exe'
    # Repeated vcvarsall initialization grows PATH/LIB in one PowerShell process.
    # Reuse the already validated environment from our previous invocation.
    $initialized = [Environment]::GetEnvironmentVariable('DOKODEMO_NATIVE_VS2022', 'Process')
    if ($initialized -ne $VsInstallPath) {
    # vcvarsall is the only command interpreted by cmd; no filesystem mutations.
    $environment = Invoke-DokodemoProcess -FilePath $cmd -RawArguments ('/d /s /c "call "' + $vcvars + '" x64 >nul && set"') -WorkingDirectory (Get-DokodemoRoot) -Quiet
    foreach ($line in ($environment.stdout -split "`r?`n")) {
        $equals = $line.IndexOf('=')
        if ($equals -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $equals), $line.Substring($equals + 1), 'Process') }
    }
    }
    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or $env:VSCMD_ARG_HOST_ARCH -ne 'x64') { throw 'vcvarsall did not configure an x64 host and target.' }
    if ($env:VisualStudioVersion -notlike '17.*') { throw 'Use a Visual Studio 2022 (17.x) installation.' }
    $cl = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
    $link = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/link.exe'
    if (-not (Test-Path -LiteralPath $cl) -or -not (Test-Path -LiteralPath $link)) { throw 'The x64 MSVC compiler/linker is missing.' }
    [Environment]::SetEnvironmentVariable('DOKODEMO_NATIVE_VS2022', $VsInstallPath, 'Process')
    # Keep inherited per-machine compiler flags out of a reproducible invocation.
    [Environment]::SetEnvironmentVariable('CL', $null, 'Process')
    [Environment]::SetEnvironmentVariable('_CL_', $null, 'Process')
    [Environment]::SetEnvironmentVariable('LINK', $null, 'Process')
    return [pscustomobject]@{
        installation_path = $VsInstallPath; installation_version = $vsVersion
        tools_version = $env:VCToolsVersion.TrimEnd('\', '/')
        sdk_version = $env:WindowsSDKVersion.TrimEnd('\', '/')
        compiler = $cl; linker = $link
        compiler_sha256 = Get-DokodemoSha256 $cl
        linker_sha256 = Get-DokodemoSha256 $link
    }
}

function Resolve-DokodemoOutputDirectory {
    param([string]$Root, [string]$Path)
    $absolute = Resolve-DokodemoPath -Root $Root -Path $Path
    $relative = Get-DokodemoRelativePath -Root $Root -Path $absolute
    if ($relative -ne 'build' -and -not $relative.StartsWith('build/', [StringComparison]::OrdinalIgnoreCase) -and -not $relative.StartsWith('research/build/', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build outputs must be inside build/ or research/build/.'
    }
    return $absolute
}
