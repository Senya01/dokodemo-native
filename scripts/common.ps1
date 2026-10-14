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

function Get-DokodemoFileIdentity {
    param([string]$Path, [string]$Root)
    $file = Get-Item -LiteralPath ([IO.Path]::GetFullPath($Path)) -ErrorAction Stop
    if ($file.PSIsContainer) { throw "Expected a file: $Path" }
    $name = $file.FullName
    if ($Root) {
        $prefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        if ($name.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $name = Get-DokodemoRelativePath -Root $Root -Path $file.FullName }
    }
    return [ordered]@{ path = $name; size_bytes = [long]$file.Length; sha256 = Get-DokodemoSha256 $file.FullName }
}

function Get-DokodemoSourceIsoIdentity {
    param([string]$Root)
    $descriptorPath = Resolve-DokodemoPath -Root $Root -Path 'source-iso.json' -MustExist
    $descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
    if ([long]$descriptor.size_bytes -ne 422805504 -or $descriptor.sha256 -cne 'c78d0974f660cd92c1b10a1466ac278625d38e4dd93bfb48f66f090ff7a4c119') {
        throw 'source-iso.json differs from the pinned Doko Demo Issyo Portable v1.01 image identity.'
    }
    $iso = Resolve-DokodemoPath -Root $Root -Path $descriptor.relative_path -MustExist
    $identity = Get-DokodemoFileIdentity -Path $iso -Root $Root
    if ($identity.size_bytes -ne [long]$descriptor.size_bytes -or $identity.sha256 -cne $descriptor.sha256.ToLowerInvariant()) {
        throw 'The protected local ISO does not match source-iso.json.'
    }
    return [ordered]@{ verified = $true; descriptor = Get-DokodemoFileIdentity -Path $descriptorPath -Root $Root; image = $identity }
}

function Get-DokodemoGitSnapshot {
    param([string]$Root, [object[]]$Inputs)
    $git = (Get-Command git.exe -ErrorAction Stop).Source
    $commit = (Invoke-DokodemoProcess -FilePath $git -Arguments @('rev-parse', '--verify', 'HEAD') -WorkingDirectory $Root -Quiet -Utf8Output).stdout.Trim()
    $tree = (Invoke-DokodemoProcess -FilePath $git -Arguments @('ls-tree', '-r', '-z', 'HEAD') -WorkingDirectory $Root -Quiet -Utf8Output).stdout
    $index = (Invoke-DokodemoProcess -FilePath $git -Arguments @('ls-files', '-z', '--cached') -WorkingDirectory $Root -Quiet -Utf8Output).stdout
    $diff = (Invoke-DokodemoProcess -FilePath $git -Arguments @('diff', '--name-only', '-z', 'HEAD', '--') -WorkingDirectory $Root -Quiet -Utf8Output).stdout
    $headEntries = @{}; $indexEntries = @{}; $dirtyEntries = @{}
    foreach ($entry in $tree.Split([char]0)) {
        if ($entry -match '^\d+ blob ([0-9a-f]+)\t([\s\S]+)$') { $headEntries[$Matches[2]] = $Matches[1] }
    }
    foreach ($name in $index.Split([char]0)) { if ($name) { $indexEntries[$name] = $true } }
    foreach ($name in $diff.Split([char]0)) { if ($name) { $dirtyEntries[$name] = $true } }
    $tracked = @(); $dirty = @(); $untracked = @()
    foreach ($input in $Inputs) {
        $entry = [ordered]@{ path = $input.path; size_bytes = $input.size_bytes; sha256 = $input.sha256 }
        if ($indexEntries.ContainsKey($input.path)) {
            $entry.head_blob = $headEntries[$input.path]
            $entry.differs_from_head = $dirtyEntries.ContainsKey($input.path)
            $tracked += $entry
            if ($entry.differs_from_head) { $dirty += $entry }
        } else {
            # Includes intentionally ignored fixture/generated inputs; absence
            # from git status does not make such a build commit-only.
            $untracked += $entry
        }
    }
    return [ordered]@{
        commit_sha = $commit
        tracked_inputs = $tracked; dirty_tracked_inputs = $dirty; untracked_needed_inputs = $untracked
        commit_only_inputs = ($dirty.Count -eq 0 -and $untracked.Count -eq 0)
        scope = 'Selected build-input snapshot, not a claim that the whole repository is clean. SHA-256 describes working bytes; head_blob identifies the Git object.'
    }
}

function Get-DokodemoExternalInventory {
    param([string]$Root, [string[]]$Paths)
    $unique = @{}
    foreach ($path in $Paths) { $unique[[IO.Path]::GetFullPath($path)] = $true }
    $names = [string[]]@($unique.Keys)
    [Array]::Sort($names, [StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $names) { Get-DokodemoFileIdentity -Path $name -Root $Root }
}

function Get-DokodemoPathSnapshot {
    param([string]$Root, [string[]]$Paths)
    $roots = @()
    foreach ($path in $Paths) {
        $absolute = Resolve-DokodemoPath -Root $Root -Path $path
        $roots += [ordered]@{ path = Get-DokodemoRelativePath -Root $Root -Path $absolute; exists = (Test-Path -LiteralPath $absolute) }
    }
    $files = @(Get-DokodemoInventory -Root $Root -Paths $Paths)
    $bytes = [long]0; foreach ($file in $files) { $bytes += $file.size_bytes }
    return [ordered]@{ roots = $roots; file_count = $files.Count; size_bytes = $bytes; files = $files }
}

function Get-DokodemoBootstrapProvenance {
    param([string]$Root, [object[]]$SourceAssets = @(), [switch]$VerifyOutputs, [switch]$ExecutedByBuild, [string]$ReceiptPath)
    if (-not $ReceiptPath) {
        $ReceiptPath = if (Test-Path -LiteralPath (Join-Path $Root 'research/build/bootstrap-manifest-v2.json')) { 'research/build/bootstrap-manifest-v2.json' } else { 'research/build/bootstrap-manifest.json' }
    }
    $manifestPath = Resolve-DokodemoPath -Root $Root -Path $ReceiptPath
    $unknowns = @('Declared input/output snapshots are not a dynamic file access trace; undeclared dependencies remain unknown.', 'Historical clean-bootstrap evidence is not proof of a clean bootstrap for this build.')
    $result = [ordered]@{
        executed_by_this_build = [bool]$ExecutedByBuild; clean_bootstrap_verified_for_this_build = $false
        complete_tool_input_output_chain = $false; status = 'receipt_absent'; unknowns = $unknowns
        receipt = $null; tools = @(); output_counts = [ordered]@{ verified = 0; recorded = 0; mismatched = 0; missing = 0; unchecked = 0 }
    }
    if (-not (Test-Path -LiteralPath $manifestPath)) { return $result }
    $result.receipt = Get-DokodemoFileIdentity -Path $manifestPath -Root $Root
    $receipt = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($receipt.schema_version -notin @(1, 2)) { $result.status = 'unsupported_receipt_schema'; return $result }
    $result.receipt_schema_version = $receipt.schema_version
    if ($receipt.schema_version -eq 1) { $result.unknowns += 'The historical schema-1 receipt lacks per-step input snapshots and Python binary identity.' }
    else {
        $result.python = $receipt.python
        $result.wrapper_inputs = $receipt.wrapper_inputs
        $result.declared_step_chain = @($receipt.results | Where-Object { $_.status -eq 'prepared' } | ForEach-Object {
            [ordered]@{ id = $_.id; tool = $_.tool; tool_sha256 = $_.tool_sha256; declaration_source = $_.declaration_source; input_files = $_.declared_inputs_before.file_count; prerequisite_files = $_.prerequisites_before.file_count; output_files = @($_.outputs).Count; output_state = $_.output_state }
        })
        $result.unknowns += @($receipt.unknowns)
    }
    $result.recorded_step_chain = @($receipt.results | Where-Object { $_.status -eq 'prepared' } | ForEach-Object {
        [ordered]@{ id = $_.id; tool = $_.tool; tool_sha256 = $_.tool_sha256; arguments = $_.arguments; output_files = @($_.outputs).Count; input_snapshot_state = $(if ($receipt.schema_version -eq 2) { 'declared_snapshots_in_linked_receipt' } else { 'not_recorded_in_historical_receipt' }) }
    })
    $result.status = 'historical_receipt_audited'
    $currentIso = Get-Content -LiteralPath (Join-Path $Root 'source-iso.json') -Raw | ConvertFrom-Json
    $result.source_matches_descriptor = ($receipt.source.sha256 -ceq $currentIso.sha256 -and [long]$receipt.source.size_bytes -eq [long]$currentIso.size_bytes)
    $pipeline = Resolve-DokodemoPath -Root $Root -Path $receipt.pipeline.path -MustExist
    $result.pipeline = Get-DokodemoFileIdentity -Path $pipeline -Root $Root
    $result.pipeline_matches_receipt = ($result.pipeline.sha256 -ceq $receipt.pipeline.sha256)
    foreach ($input in $receipt.tool_inputs) {
        $path = Resolve-DokodemoPath -Root $Root -Path $input.path
        $exists = Test-Path -LiteralPath $path -PathType Leaf
        $identity = if ($exists) { Get-DokodemoFileIdentity -Path $path -Root $Root } else { $null }
        $result.tools += [ordered]@{ path = $input.path; recorded_sha256 = $input.sha256; current = $identity; matches_receipt = ($exists -and $identity.sha256 -ceq $input.sha256) }
    }
    $assets = @{}; foreach ($asset in $SourceAssets) { $assets[$asset.path] = $asset }
    $result.producer_receipts = @($SourceAssets | Where-Object { $_.path -match '/(provenance|[^/]*manifest|catalog)\.json$' })
    if ($receipt.schema_version -eq 2) {
        $result.declared_input_checks = [ordered]@{ verified = 0; recorded = 0; mismatched_or_missing = 0; unchecked = 0 }
        $inputFiles = @($receipt.wrapper_inputs) + @($receipt.python.binary) + @($receipt.python.runtime_files)
        foreach ($step in $receipt.results) {
            if ($step.status -eq 'prepared') {
                $inputFiles += @($step.declared_inputs_before.files) + @($step.prerequisites_before.files)
                foreach ($external in $step.external_tools) { $inputFiles += $external.identity }
            }
        }
        $currentFiles = @{}
        foreach ($input in $inputFiles) {
            $result.declared_input_checks.recorded++
            if (-not $VerifyOutputs) { $result.declared_input_checks.unchecked++; continue }
            if (-not $currentFiles.ContainsKey($input.path)) {
                # Only runtime binaries may live outside the workspace. Every
                # path came from a receipt; read it as a literal, never execute.
                $path = if ([IO.Path]::IsPathRooted($input.path)) { $input.path } else { Resolve-DokodemoPath -Root $Root -Path $input.path }
                $currentFiles[$input.path] = if ($assets.ContainsKey($input.path)) { $assets[$input.path] } elseif (Test-Path -LiteralPath $path -PathType Leaf) { Get-DokodemoFileIdentity -Path $path -Root $Root } else { $null }
            }
            $actual = $currentFiles[$input.path]
            if ($actual -and $actual.sha256 -ceq $input.sha256 -and $actual.size_bytes -eq $input.size_bytes) { $result.declared_input_checks.verified++ }
            else { $result.declared_input_checks.mismatched_or_missing++ }
        }
    }
    $mismatches = @()
    foreach ($step in $receipt.results) {
        if ($step.status -ne 'prepared') { continue }
        foreach ($output in $step.outputs) {
            $result.output_counts.recorded++
            if (-not $VerifyOutputs) { $result.output_counts.unchecked++; continue }
            $path = Resolve-DokodemoPath -Root $Root -Path $output.path
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                $result.output_counts.missing++; $mismatches += $output.path; continue
            }
            $actual = if ($assets.ContainsKey($output.path)) { $assets[$output.path] } else { Get-DokodemoFileIdentity -Path $path -Root $Root }
            if ($actual.sha256 -cne $output.sha256 -or $actual.size_bytes -ne $output.size_bytes) {
                $result.output_counts.mismatched++; $mismatches += $output.path
            } else { $result.output_counts.verified++ }
        }
    }
    $result.output_mismatches_or_missing = $mismatches
    $result.output_count_scope = 'Recorded per-step file entries; repeated paths across step snapshots are not distinct source bytes.'
    $result.recorded_tools_match = (@($result.tools | Where-Object { -not $_.matches_receipt }).Count -eq 0)
    $result.input_lineage_status = 'incomplete_historical_input_record'
    if ($receipt.schema_version -eq 2) {
        $result.input_lineage_status = if (-not $VerifyOutputs) { 'not_reverified' } elseif ($result.declared_input_checks.mismatched_or_missing -gt 0 -or -not $result.pipeline_matches_receipt -or -not $result.source_matches_descriptor -or -not $result.recorded_tools_match) { 'mismatch' } else { 'verified_declared_inputs' }
    }
    if ($VerifyOutputs -and $result.output_counts.recorded -gt 0 -and $result.output_counts.mismatched -eq 0 -and $result.output_counts.missing -eq 0) { $result.status = 'existing_outputs_verified' }
    if (-not $VerifyOutputs) { $result.status = 'receipt_linked_outputs_unchecked' }
    return $result
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
    $support = @()
    foreach ($name in @('c1xx.dll', 'c2.dll', 'msobj140.dll', 'mspdb140.dll', 'mspdbcore.dll', 'mspdbsrv.exe', '1033/clui.dll', '1033/linkui.dll')) {
        $path = Join-Path ([IO.Path]::GetDirectoryName($cl)) $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $identity = Get-DokodemoFileIdentity -Path $path
            $identity.file_version = (Get-Item -LiteralPath $path).VersionInfo.FileVersion
            $support += $identity
        }
    }
    return [pscustomobject]@{
        installation_path = $VsInstallPath; installation_version = $vsVersion
        tools_version = $env:VCToolsVersion.TrimEnd('\', '/')
        sdk_version = $env:WindowsSDKVersion.TrimEnd('\', '/')
        compiler = $cl; linker = $link
        compiler_sha256 = Get-DokodemoSha256 $cl
        linker_sha256 = Get-DokodemoSha256 $link
        compiler_file_version = (Get-Item -LiteralPath $cl).VersionInfo.FileVersion
        linker_file_version = (Get-Item -LiteralPath $link).VersionInfo.FileVersion
        environment_script = Get-DokodemoFileIdentity -Path $vcvars
        sdk_root = $env:WindowsSdkDir
        include_directories = @($env:INCLUDE -split ';' | Where-Object { $_ })
        library_directories = @($env:LIB -split ';' | Where-Object { $_ })
        compiler_support_files = $support
        support_scope = 'Selected MSVC frontend/backend and support binaries; this is not a process-wide loaded-DLL trace or a complete Windows OS snapshot.'
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
