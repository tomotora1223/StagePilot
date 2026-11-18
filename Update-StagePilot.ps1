param(
    [Parameter(Mandatory=$true)][string] $CurrentVersion,
    [Parameter(Mandatory=$true)][int] $AppProcessId
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
# Use Windows PowerShell's own modules, even when started from PowerShell 7.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
Import-Module (Join-Path $env:PSModulePath 'Microsoft.PowerShell.Management') -ErrorAction Stop
Import-Module (Join-Path $env:PSModulePath 'Microsoft.PowerShell.Utility') -ErrorAction Stop
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$appDirectory = $PSScriptRoot
$targetExe = Join-Path $appDirectory 'StagePilot.exe'
if ((Test-Path -LiteralPath (Join-Path $appDirectory 'no-auto-update')) -or !(Test-Path -LiteralPath $targetExe -PathType Leaf)) { exit 0 }

$repo = 'tomotora1223/StagePilot'
$apiHeaders = @{ 'User-Agent' = 'StagePilot-Updater'; 'Accept' = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' }
$staging = $null
$locked = $false
$mutex = New-Object Threading.Mutex($false, 'Local\StagePilotReleaseUpdater')
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { exit 0 }
    $buildHash = (Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers $apiHeaders -TimeoutSec 15
    if ($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^v(\d+\.\d+(?:\.\d+)?)$') { exit 0 }
    $releaseVersion = $Matches[1]
    function Normalize-Version([string] $value) {
        $version = [version]$value
        return [version]::new($version.Major, $version.Minor, [Math]::Max(0, $version.Build))
    }
    if ((Normalize-Version $releaseVersion) -le (Normalize-Version $CurrentVersion)) { exit 0 }
    $assetName = "StagePilot-$releaseVersion-Windows-x64.zip"
    $asset = @($release.assets | Where-Object { $_.name -ceq $assetName })
    if ($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:([a-fA-F0-9]{64})$') {
        throw 'Release ZIP or SHA-256 digest missing'
    }
    $expectedHash = $Matches[1]
    $expectedUrl = "https://github.com/$repo/releases/download/$($release.tag_name)/$assetName"
    if ($asset[0].browser_download_url -cne $expectedUrl -or $asset[0].size -lt 1 -or $asset[0].size -gt 268435456) {
        throw 'Unexpected release asset'
    }
    $staging = Join-Path $appDirectory ('.stagepilot-update-' + [guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $staging)
    $zip = Join-Path $staging $assetName
    Invoke-WebRequest -Uri $expectedUrl -UseBasicParsing -OutFile $zip -TimeoutSec 120
    if ((Get-Item -LiteralPath $zip).Length -ne $asset[0].size -or
        (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Release ZIP checksum mismatch' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $incoming = Join-Path $staging 'StagePilot.exe'
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entry = $archive.GetEntry("StagePilot-v$releaseVersion/StagePilot.exe")
        if (!$entry -or $entry.Length -le 0 -or $entry.Length -gt 134217728) { throw 'Release executable missing' }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $incoming)
    } finally {
        $archive.Dispose()
    }
    $process = Get-Process -Id $AppProcessId -ErrorAction SilentlyContinue
    if ($process -and $process.Path -eq $targetExe) {
        # Poll so an opted-out installation can release its updater while the app is still open.
        while (!$process.WaitForExit(1000)) {
            if (Test-Path -LiteralPath (Join-Path $appDirectory 'no-auto-update')) { exit 0 }
        }
    }
    if (Test-Path -LiteralPath (Join-Path $appDirectory 'no-auto-update')) { exit 0 }
    if ((Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash -ne $buildHash) { exit 0 }
    [IO.File]::Replace($incoming, $targetExe, (Join-Path $appDirectory 'StagePilot.previous.exe'))
    Add-Content -LiteralPath (Join-Path $appDirectory 'update.log') -Encoding UTF8 -Value "Installed v$releaseVersion"
} catch {
    try { Add-Content -LiteralPath (Join-Path $appDirectory 'update.log') -Encoding UTF8 -Value $_.Exception.Message } catch {}
    exit 1
} finally {
    if ($staging) {
        foreach ($fileName in @($assetName, 'StagePilot.exe')) {
            if ($fileName) { Remove-Item -LiteralPath (Join-Path $staging $fileName) -ErrorAction SilentlyContinue }
        }
        Remove-Item -LiteralPath $staging -ErrorAction SilentlyContinue
    }
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
