param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$')][string]$Model,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Result
)
$ErrorActionPreference = 'Stop'
$server = $null
$resultIsSafe = $false
$stage = $null
$archive = $null
function Owned-Path([string]$Value) {
    $full = [IO.Path]::GetFullPath($Value)
    if (-not $full.StartsWith($script:rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Path must stay inside the translator directory'
    }
    return $full
}
function Install-Runtime([string]$Runtime) {
    # Fixed artifact, never silently replace a reviewed input with releases/latest.
    # Publisher-direct optional download; retain all notices, never relabel GPU DLLs MIT.
    $runtimeVersion = 'v0.40.13'
    $runtimeUrl = 'https://github.com/ollama/ollama/releases/download/v0.40.13/ollama-windows-amd64.zip'
    $expected = '7afa38624fa62ee5d8293422d0ab084c1bb4603134c7f4dafce11a033b57cc19'
    $script:archive = Owned-Path (Join-Path $script:data 'Downloads\ollama-runtime.zip')
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($script:archive)) -Force | Out-Null
    Write-Host 'Downloading official portable Ollama to the translator folder...'
    Write-Host 'Official runtime component terms apply separately; see the component notices. Custom model terms are independent.'
    Invoke-WebRequest -Uri $runtimeUrl -OutFile $script:archive -UseBasicParsing -TimeoutSec 3600
    $archiveStream = [IO.File]::OpenRead($script:archive)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $actual = [BitConverter]::ToString($hasher.ComputeHash($archiveStream)).Replace('-', '') }
    finally { $archiveStream.Dispose(); $hasher.Dispose() }
    if ($actual -ne $expected) { throw 'Ollama archive SHA-256 mismatch' }
    $script:stage = Owned-Path (Join-Path $script:rootPath ('data\ollama-staging-' + [Guid]::NewGuid().ToString('N')))
    New-Item -ItemType Directory -Path $script:stage -Force | Out-Null
    [Reflection.Assembly]::LoadWithPartialName('System.IO.Compression.FileSystem') | Out-Null
    $zip = [IO.Compression.ZipFile]::OpenRead($script:archive)
    try {
        [long]$total = 0
        foreach ($entry in $zip.Entries) {
            $target = [IO.Path]::GetFullPath((Join-Path $script:stage $entry.FullName))
            if (-not $target.StartsWith($script:stage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Unsafe runtime archive path' }
            $total += $entry.Length
            if ($total -gt 16GB) { throw 'Runtime archive exceeds extraction limit' }
            if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) {
                [IO.Directory]::CreateDirectory($target) | Out-Null
            } else {
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
                $inputStream = $entry.Open()
                try {
                    $outputStream = [IO.File]::Create($target)
                    try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
                } finally { $inputStream.Dispose() }
            }
        }
    } finally { $zip.Dispose() }
    if (-not (Test-Path -LiteralPath (Join-Path $script:stage 'ollama.exe') -PathType Leaf)) { throw 'Portable Ollama binary is missing' }
    # This exact official ZIP has no root LICENSE. Its four original notices are
    # inside lib/ollama. Check the actual layout rather than an invented root file.
    foreach ($notice in @('GO_LICENSE','CPP_HTTPLIB_LICENSE','LLAMA_CPP_LICENSE','LLAMA_CPP_VENDORS_LICENSE')) {
        if (-not (Test-Path -LiteralPath (Join-Path $script:stage ('lib\ollama\' + $notice)) -PathType Leaf)) { throw 'Portable Ollama component notice is missing' }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ollama-runtime-license.txt') -Destination (Join-Path $script:stage 'LICENSE')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ollama-runtime-notices.txt') -Destination (Join-Path $script:stage 'PROJECT_COMPONENT_NOTICES.txt')
    @{ version=$runtimeVersion; url=$runtimeUrl; sha256=$expected; licenseReview='publisher-direct-inventory-reviewed'; distribution='direct-from-upstream-not-project-redistribution' } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $script:stage 'runtime-source.json') -Encoding UTF8
    if (Test-Path -LiteralPath $Runtime) { throw 'Incomplete runtime directory exists; remove data\ollama and retry' }
    Move-Item -LiteralPath $script:stage -Destination $Runtime
    $script:stage = $null
}
try {
    if (-not [IO.Path]::IsPathRooted($Root)) { throw 'An absolute translator directory is required' }
    $script:rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    $script:data = Owned-Path (Join-Path $script:rootPath 'UserData')
    $Result = Owned-Path $Result
    if (-not $Result.StartsWith($script:data + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Result must stay in UserData' }
    $resultIsSafe = $true
    foreach ($sub in @('Temp','Logs','ollama\models','ollama-home','ollama-home\AppData\Local','ollama-home\AppData\Roaming')) {
        New-Item -ItemType Directory -Path (Join-Path $script:data $sub) -Force | Out-Null
    }
    $env:TEMP = $env:TMP = Join-Path $script:data 'Temp'
    $ollamaProfile = Join-Path $script:data 'ollama-home'
    $env:USERPROFILE = $ollamaProfile
    $env:LOCALAPPDATA = Join-Path $ollamaProfile 'AppData\Local'
    $env:APPDATA = Join-Path $ollamaProfile 'AppData\Roaming'
    $env:OLLAMA_MODELS = Join-Path $script:data 'ollama\models'
    $env:OLLAMA_HOST = '127.0.0.1:11435'
    $env:OLLAMA_NO_CLOUD = '1'
    $env:PSModuleAnalysisCachePath = Join-Path $script:data 'Temp\ps-module-cache'
    Remove-Item -LiteralPath $Result -ErrorAction SilentlyContinue
    $runtime = Owned-Path (Join-Path $script:rootPath 'data\ollama')
    $ollama = Join-Path $runtime 'ollama.exe'
    if (-not (Test-Path -LiteralPath $ollama -PathType Leaf)) { Install-Runtime $runtime }
    $listeners = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
    if (@($listeners | Where-Object { $_.Port -eq 11435 }).Count) { throw 'Portable Ollama port 11435 is occupied' }
    $server = Start-Process -FilePath $ollama -ArgumentList 'serve' -WorkingDirectory $runtime -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $script:data 'Logs\ollama-setup-server.log') `
        -RedirectStandardError (Join-Path $script:data 'Logs\ollama-setup-server-error.log')
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        $server.Refresh()
        if ($server.HasExited) { throw 'Portable Ollama exited before becoming ready' }
        try {
            $version = Invoke-RestMethod -Uri 'http://127.0.0.1:11435/api/version' -TimeoutSec 2
            if ($version.version) { $ready = $true; break }
        } catch {}
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'Portable Ollama did not start in 30 seconds' }
    $modelName = if (($Model -split '/')[-1].Contains(':')) { $Model } else { "${Model}:latest" }
    $tags = Invoke-RestMethod -Uri 'http://127.0.0.1:11435/api/tags' -TimeoutSec 10
    if (-not @($tags.models | Where-Object { $_.name -eq $modelName }).Count) {
        Write-Host "Downloading $Model into $env:OLLAMA_MODELS ..."
        & $ollama pull $Model
        if ($LASTEXITCODE -ne 0) { throw "Model download failed, exit code $LASTEXITCODE" }
    }
    $tags = Invoke-RestMethod -Uri 'http://127.0.0.1:11435/api/tags' -TimeoutSec 10
    if (-not @($tags.models | Where-Object { $_.name -eq $modelName }).Count) { throw 'Selected model is absent after download' }
    # Record the actual installed model digest and supplied terms at download time.
    # A custom model with no license is recorded as unknown, never as automatically MIT.
    $shown = Invoke-RestMethod -Uri 'http://127.0.0.1:11435/api/show' -Method Post -ContentType 'application/json' `
        -Body (@{model=$modelName} | ConvertTo-Json -Compress) -TimeoutSec 30
    $installedModel = @($tags.models | Where-Object { $_.name -eq $modelName }) | Select-Object -First 1
    $nameHasher = [Security.Cryptography.SHA256]::Create()
    try { $noticeId = [BitConverter]::ToString($nameHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($modelName))).Replace('-','').ToLowerInvariant() }
    finally { $nameHasher.Dispose() }
    $noticeDirectory = Owned-Path (Join-Path $script:data 'ollama\model-notices')
    New-Item -ItemType Directory -Path $noticeDirectory -Force | Out-Null
    @{model=$modelName; digest=$installedModel.digest; license=$shown.license;
      source='Ollama local /api/tags and /api/show after explicit preparation';
      review='License supplied by model publisher; custom terms require separate review'} |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $noticeDirectory ($noticeId + '.json')) -Encoding UTF8
    [IO.File]::WriteAllText($Result, 'OK', [Text.Encoding]::UTF8)
    Write-Host 'Portable Ollama and selected model are ready.'
    $exitCode = 0
} catch {
    $message = $_.Exception.Message
    if ($resultIsSafe) { [IO.File]::WriteAllText($Result, "ERROR: $message", [Text.Encoding]::UTF8) }
    Write-Host $message -ForegroundColor Red
    $exitCode = 1
} finally {
    if ($server) { $server.Refresh(); if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue } }
    if ($archive -and (Test-Path -LiteralPath $archive)) { Remove-Item -LiteralPath (Owned-Path $archive) -Force }
    if ($stage -and (Test-Path -LiteralPath $stage)) {
        $safeStage = Owned-Path $stage
        if ([IO.Path]::GetFileName($safeStage) -notlike 'ollama-staging-*') { throw 'Unsafe staging cleanup' }
        Remove-Item -LiteralPath $safeStage -Recurse -Force
    }
}
exit $exitCode
