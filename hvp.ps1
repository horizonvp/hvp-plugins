<#
.SYNOPSIS
    Sync the HVP plugins between an Unreal project and the hvp-plugins repository.

.DESCRIPTION
    The plugins live in a project as ordinary tracked files under Plugins/HVP, brought in with
    git subtree. Most people never run this script: to them the project is one git repo.
    The few who move plugin changes between projects use these verbs:

      hvp.ps1 status                    what version is in this project, and has it drifted
      hvp.ps1 pull  <tag|branch>        bring a plugin release into this project (choose when)
      hvp.ps1 push  <branch>            send plugin edits made in this project to the plugin repo
      hvp.ps1 add   <tag|branch>        first-time adoption in a project that has the old copies

    The script adds the 'hvp' git remote on first use, so nothing needs setting up by hand.
    Run it from the copy inside the project (Plugins/HVP/hvp.ps1), or from a clone of the
    plugin repo with -Project <path> for a project that has no copy yet.

.PARAMETER Ref
    A tag such as v1.2.0, or a branch, in the plugin repository.

.PARAMETER Project
    Project root. Defaults to the git repository this script lives in.

.PARAMETER RemoveLegacy
    With 'add': delete the per-project Plugins/HVPSystems and Plugins/Horizon* copies first.

.PARAMETER ApplyRedirects
    With 'add': rewrite the .uproject plugin list, append the rename redirects to
    Config/DefaultEngine.ini and fix the settings section names in Config/DefaultEditor.ini.

.PARAMETER DryRun
    Print every git command without running the ones that change anything.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('status', 'pull', 'push', 'add', 'help')]
    [string]$Command = 'help',

    [Parameter(Position = 1)]
    [string]$Ref,

    [string]$Project,
    [string]$Remote = 'hvp',
    [string]$Url,
    [string]$Prefix,
    [switch]$RemoveLegacy,
    [switch]$ApplyRedirects,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$ScriptDir = $PSScriptRoot
$DefaultUrl = 'git@github.com:horizonvp/hvp-plugins.git'
$DefaultPrefix = 'Plugins/HVP'

$LegacyPlugins = @(
    'HVPSystems', 'HorizonStereoButton', 'HorizonNamingConventions',
    'HorizonGraphSelect', 'HorizonPalette', 'HorizonPrimitiveData'
)
$NewPlugins = @('HVPSystems', 'HVPStereoButton', 'HVPEditor', 'HVPPrimitiveData')

$RedirectBlock = @'

[CoreRedirects]
; --- HVP plugins: renames from the per-project Horizon* copies (added by hvp.ps1) ---
; Remove this block once every asset that referenced the old names has been resaved.
+PackageRedirects=(OldName="/Script/HorizonStereoButton",NewName="/Script/HVPStereoButton")
+PackageRedirects=(OldName="/Script/HorizonStereoButtonEditor",NewName="/Script/HVPStereoButtonEditor")
+PackageRedirects=(OldName="/HorizonStereoButton/LenovoButtonFace_M",NewName="/HVPStereoButton/StereoButtonFace_M")
+ObjectRedirects=(OldName="/HorizonStereoButton/LenovoButtonFace_M.LenovoButtonFace_M",NewName="/HVPStereoButton/StereoButtonFace_M.StereoButtonFace_M")
; MatchWildcard needs 5.8; on 5.7 write OldName="/HorizonStereoButton/" with MatchSubstring=true instead.
+PackageRedirects=(OldName="/HorizonStereoButton/...",NewName="/HVPStereoButton/",MatchWildcard=true)
+PackageRedirects=(OldName="/Script/HorizonPrimitiveDataUncooked",NewName="/Script/HVPPrimitiveDataUncooked")
+PackageRedirects=(OldName="/Script/HorizonPrimitiveDataEditor",NewName="/Script/HVPPrimitiveDataEditor")
'@

# ---------------------------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------------------------

function Write-Step([string]$Message) { Write-Host "== $Message" -ForegroundColor Cyan }
function Write-Note([string]$Message) { Write-Host "   $Message" -ForegroundColor DarkGray }

# Runs a git command that changes something, echoing it first. Throws on a non-zero exit and
# does nothing under -DryRun. Deliberately has no named parameters of its own: PowerShell binds
# by prefix, so a switch called -ReadOnly would silently eat git's -r.
function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$GitArgs)
    Write-Host "> git $($GitArgs -join ' ')" -ForegroundColor DarkGray
    if ($DryRun) { return @() }
    $output = & git @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "git $($GitArgs -join ' ') failed with exit code $LASTEXITCODE" }
    return $output
}

# Quiet read that tolerates failure: returns $null instead of throwing. Reads always run, even
# under -DryRun, so the script can reason about the repository.
function Get-GitValue {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$GitArgs)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git @GitArgs
        if ($LASTEXITCODE -ne 0) { return $null }
        return $output
    }
    finally { $ErrorActionPreference = $previous }
}

function Resolve-ProjectRoot {
    if ($Project) {
        $root = (Resolve-Path $Project).Path
        $top = Get-GitValue -C $root rev-parse --show-toplevel
        if (-not $top) { throw "$root is not inside a git repository" }
        return (Resolve-Path $top).Path
    }
    $top = Get-GitValue -C $ScriptDir rev-parse --show-toplevel
    if (-not $top) { throw "hvp.ps1 is not inside a git repository; pass -Project <path>" }
    $top = (Resolve-Path $top).Path
    if ((Resolve-Path $ScriptDir).Path.TrimEnd('\') -eq $top.TrimEnd('\')) {
        throw "hvp.ps1 is running from the plugin repository itself. Pass -Project <path> to act on a project."
    }
    return $top
}

function Resolve-Prefix([string]$Root) {
    if ($Prefix) { return $Prefix.Replace('\', '/').Trim('/') }
    if ($Project) { return $DefaultPrefix }
    # The script lives inside the subtree: the prefix is its own location relative to the root.
    $relative = (Resolve-Path $ScriptDir).Path.Substring($Root.TrimEnd('\').Length).Trim('\')
    if (-not $relative) { return $DefaultPrefix }
    return $relative.Replace('\', '/')
}

function Resolve-Url {
    if ($Url) { return $Url }
    # Prefer the origin of the plugin repo this script came from, when it came from one.
    $top = Get-GitValue -C $ScriptDir rev-parse --show-toplevel
    if ($top) {
        $marker = Join-Path $top 'HVPHost/HVPHost.uproject'
        if ((Test-Path $marker) -and (@(Get-GitValue -C $top remote) -contains 'origin')) {
            $origin = Get-GitValue -C $top remote get-url origin
            if ($origin) { return $origin }
        }
    }
    return $DefaultUrl
}

function Assert-CleanTree {
    $dirty = Get-GitValue status --porcelain --untracked-files=no
    if ($dirty) { throw "Working tree has uncommitted changes. Commit or stash them first.`n$($dirty -join "`n")" }
}

function Assert-OnBranch {
    $branch = Get-GitValue rev-parse --abbrev-ref HEAD
    if (-not $branch -or $branch -eq 'HEAD') { throw 'Detached HEAD. Check out a branch first.' }
}

function Ensure-Remote([string]$RemoteUrl) {
    $remotes = @(Get-GitValue remote)
    if ($remotes -contains $Remote) {
        Write-Note "remote '$Remote' -> $(Get-GitValue remote get-url $Remote)"
        return
    }
    Write-Step "Adding remote '$Remote' ($RemoteUrl)"
    Invoke-Git remote add $Remote $RemoteUrl | Out-Null
}

# The squash commit git subtree writes carries "git-subtree-dir: <prefix>" and
# "git-subtree-split: <sha>" in its body. That is the import marker.
function Get-LastImport([string]$SubtreePrefix) {
    $sha = Get-GitValue log -1 --format=%H --grep="git-subtree-dir: $SubtreePrefix`$"
    if (-not $sha) { return $null }
    $body = Get-GitValue log -1 --format=%B $sha
    $split = $null
    foreach ($line in $body) {
        if ($line -match 'git-subtree-split:\s*([0-9a-f]+)') { $split = $Matches[1] }
    }
    return [pscustomobject]@{ Commit = $sha; Split = $split }
}

function Get-VersionFilePath([string]$Root, [string]$SubtreePrefix) {
    return Join-Path $Root ($SubtreePrefix.Replace('/', '\') + '.version')
}

function Write-VersionFile([string]$Root, [string]$SubtreePrefix, [string]$ImportedRef) {
    $import = Get-LastImport $SubtreePrefix
    $path = Get-VersionFilePath $Root $SubtreePrefix
    $lines = @(
        "# Written by hvp.ps1. Which release of the HVP plugins this project carries.",
        "ref=$ImportedRef",
        "split=$(if ($import) { $import.Split } else { 'unknown' })",
        "imported=$(Get-Date -Format 'yyyy-MM-dd')"
    )
    Write-Step "Recording $ImportedRef in $(Split-Path -Leaf $path)"
    if (-not $DryRun) {
        Set-Content -Path $path -Value $lines -Encoding ASCII
    }
    Invoke-Git add -- $path | Out-Null
    Invoke-Git commit -q -m "HVP plugins: record $ImportedRef" -- $path | Out-Null
}

function Read-VersionFile([string]$Root, [string]$SubtreePrefix) {
    $path = Get-VersionFilePath $Root $SubtreePrefix
    if (-not (Test-Path $path)) { return $null }
    $result = @{}
    foreach ($line in Get-Content $path) {
        if ($line -match '^\s*([^#=]+)=(.*)$') { $result[$Matches[1].Trim()] = $Matches[2].Trim() }
    }
    return $result
}

# ---------------------------------------------------------------------------------------------
# verbs
# ---------------------------------------------------------------------------------------------

function Show-Status([string]$Root, [string]$SubtreePrefix) {
    Write-Step "Project: $Root"
    Write-Note "prefix:  $SubtreePrefix"
    $remotes = @(Get-GitValue remote)
    if ($remotes -contains $Remote) { Write-Note "remote:  $(Get-GitValue remote get-url $Remote)" }
    else { Write-Note "remote:  not configured (added automatically on first pull/push)" }

    $import = Get-LastImport $SubtreePrefix
    if (-not $import) {
        Write-Host "   The HVP plugins are not in this project as a subtree." -ForegroundColor Yellow
        $legacy = $LegacyPlugins | Where-Object { Test-Path (Join-Path $Root "Plugins/$_") }
        if ($legacy) { Write-Note "per-project copies present: $($legacy -join ', ')  (use 'add')" }
        return
    }

    $version = Read-VersionFile $Root $SubtreePrefix
    if ($version) { Write-Host "   version: $($version['ref'])  (imported $($version['imported']), upstream $($version['split']))" }
    else { Write-Host "   version: unrecorded  (upstream $($import.Split))" }

    # The squash commit's tree IS the plugin repo tree, so diffing it against the subtree in
    # HEAD shows exactly what this project has changed since the import.
    $drift = Get-GitValue diff --stat $import.Commit "HEAD:$SubtreePrefix"
    if ($drift) {
        Write-Host "   Local plugin edits since import (push them with 'hvp.ps1 push <branch>'):" -ForegroundColor Yellow
        $drift | ForEach-Object { Write-Host "     $_" }
    }
    else {
        Write-Host "   No local plugin edits since import."
    }
}

function Invoke-Pull([string]$Root, [string]$SubtreePrefix) {
    if (-not $Ref) { throw "pull needs a tag or branch: hvp.ps1 pull v1.2.0" }
    Assert-CleanTree
    Assert-OnBranch
    if (-not (Get-LastImport $SubtreePrefix)) { throw "No subtree import found at $SubtreePrefix. Use 'add' for a first-time adoption." }
    Ensure-Remote (Resolve-Url)
    Write-Step "Fetching $Remote"
    Invoke-Git fetch $Remote --tags | Out-Null
    Write-Step "Pulling $Ref into $SubtreePrefix"
    Invoke-Git subtree pull --prefix=$SubtreePrefix $Remote $Ref --squash -m "HVP plugins: import $Ref" | Out-Null
    Write-VersionFile $Root $SubtreePrefix $Ref
    Write-Host "Done. Rebuild the editor target before opening the project." -ForegroundColor Green
}

function Invoke-Push([string]$Root, [string]$SubtreePrefix) {
    if (-not $Ref) { throw "push needs a target branch in the plugin repo: hvp.ps1 push fix/hand-grab-pinch" }
    if ($Ref -eq 'main') { throw "Push to a feature branch and merge it in the plugin repo, not straight to main." }
    Assert-CleanTree
    Assert-OnBranch
    if (-not (Get-LastImport $SubtreePrefix)) { throw "No subtree import found at $SubtreePrefix." }
    Ensure-Remote (Resolve-Url)
    Write-Step "Pushing $SubtreePrefix history to $Remote/$Ref"
    Write-Note "Only the plugin part of each commit is sent. Commits that also touch project code keep their full message, so keep plugin edits in their own commits."
    Invoke-Git subtree push --prefix=$SubtreePrefix $Remote $Ref | Out-Null
    Write-Host "Done. Open a pull request from $Ref in the plugin repository." -ForegroundColor Green
}

function Update-ProjectFiles([string]$Root) {
    $uproject = Get-ChildItem -Path $Root -Filter *.uproject | Select-Object -First 1
    if ($uproject) {
        Write-Step "Updating plugin list in $($uproject.Name)"
        $json = Get-Content $uproject.FullName -Raw | ConvertFrom-Json
        $plugins = @()
        if ($json.PSObject.Properties['Plugins']) {
            $plugins = @($json.Plugins | Where-Object { $LegacyPlugins -notcontains $_.Name })
        }
        foreach ($name in $NewPlugins) {
            if (-not ($plugins | Where-Object { $_.Name -eq $name })) {
                $entry = [ordered]@{ Name = $name; Enabled = $true }
                if ($name -eq 'HVPEditor') { $entry['TargetAllowList'] = @('Editor') }
                $plugins += [pscustomobject]$entry
            }
        }
        if ($json.PSObject.Properties['Plugins']) { $json.Plugins = $plugins }
        else { $json | Add-Member -NotePropertyName Plugins -NotePropertyValue $plugins }
        if (-not $DryRun) {
            ($json | ConvertTo-Json -Depth 20) | Set-Content -Path $uproject.FullName -Encoding UTF8
        }
    }

    $engineIni = Join-Path $Root 'Config/DefaultEngine.ini'
    if (Test-Path $engineIni) {
        $text = Get-Content $engineIni -Raw
        if ($text -notmatch '/Script/HVPStereoButton') {
            Write-Step "Appending rename redirects to Config/DefaultEngine.ini"
            if (-not $DryRun) { Add-Content -Path $engineIni -Value $RedirectBlock -Encoding UTF8 }
        }
        else { Write-Note "Config/DefaultEngine.ini already has the redirects" }
    }

    $editorIni = Join-Path $Root 'Config/DefaultEditor.ini'
    if (Test-Path $editorIni) {
        $text = Get-Content $editorIni -Raw
        $updated = $text `
            -replace '/Script/HorizonNamingConventions\.', '/Script/HVPNamingConventions.' `
            -replace '/Script/HorizonGraphSelectEditor\.HorizonGraphSelectSettings', '/Script/HVPGraphSelectEditor.HVPGraphSelectSettings' `
            -replace '/Script/HorizonPaletteEditor\.HorizonPaletteSettings', '/Script/HVPPaletteEditor.HVPPaletteSettings'
        if ($updated -ne $text) {
            Write-Step "Renaming settings sections in Config/DefaultEditor.ini"
            if (-not $DryRun) { Set-Content -Path $editorIni -Value $updated -Encoding UTF8 -NoNewline }
        }
    }

    Invoke-Git add -A -- Config *.uproject | Out-Null
    $staged = Get-GitValue diff --cached --name-only
    if ($staged) { Invoke-Git commit -q -m "HVP plugins: point project at the HVP plugin names" | Out-Null }
}

function Invoke-Add([string]$Root, [string]$SubtreePrefix) {
    if (-not $Ref) { throw "add needs a tag or branch: hvp.ps1 add v1.0.0" }
    Assert-CleanTree
    Assert-OnBranch
    if (Get-LastImport $SubtreePrefix) { throw "Already imported at $SubtreePrefix. Use 'pull' to update." }
    if (Test-Path (Join-Path $Root $SubtreePrefix)) { throw "$SubtreePrefix already exists. git subtree add needs the path to be absent." }

    $legacy = @($LegacyPlugins | Where-Object { Test-Path (Join-Path $Root "Plugins/$_") })
    if ($legacy.Count -gt 0) {
        if (-not $RemoveLegacy) {
            throw "Per-project copies exist: $($legacy -join ', '). Re-run with -RemoveLegacy to delete them in their own commit first."
        }
        Write-Step "Removing per-project copies: $($legacy -join ', ')"
        foreach ($name in $legacy) { Invoke-Git rm -r -q -- "Plugins/$name" | Out-Null }
        Invoke-Git commit -q -m "HVP plugins: remove per-project copies ($($legacy -join ', '))" | Out-Null
        if (-not $DryRun) {
            foreach ($name in $legacy) {
                $leftover = Join-Path $Root "Plugins/$name"
                if (Test-Path $leftover) { Remove-Item -Recurse -Force $leftover }   # Binaries/Intermediate were never tracked
            }
        }
    }

    Ensure-Remote (Resolve-Url)
    Write-Step "Fetching $Remote"
    Invoke-Git fetch $Remote --tags | Out-Null
    Write-Step "Adding $Ref at $SubtreePrefix"
    Invoke-Git subtree add --prefix=$SubtreePrefix $Remote $Ref --squash -m "HVP plugins: import $Ref" | Out-Null
    Write-VersionFile $Root $SubtreePrefix $Ref

    if ($ApplyRedirects) { Update-ProjectFiles $Root }
    else {
        Write-Host "The project still names the old plugins. Re-run with -ApplyRedirects, or by hand:" -ForegroundColor Yellow
        Write-Note "1. .uproject: replace Horizon* plugin entries with $($NewPlugins -join ', ')"
        Write-Note "2. Config/DefaultEngine.ini: append the [CoreRedirects] block from hvp.ps1"
        Write-Note "3. Config/DefaultEditor.ini: rename /Script/Horizon* settings sections to /Script/HVP*"
    }

    Write-Host ""
    Write-Host "Next:" -ForegroundColor Green
    Write-Note "rebuild the editor target, open the project, and resave assets that referenced the old plugin names"
    Write-Note "(right-click the affected folders > Resave, or run -run=ResavePackages), then delete the redirect block"
}

function Show-Help {
    Get-Help $PSCommandPath -Detailed
}

# ---------------------------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------------------------

if ($Command -eq 'help') { Show-Help; return }

$root = Resolve-ProjectRoot
$prefix = Resolve-Prefix $root

Push-Location $root
try {
    switch ($Command) {
        'status' { Show-Status $root $prefix }
        'pull'   { Invoke-Pull $root $prefix }
        'push'   { Invoke-Push $root $prefix }
        'add'    { Invoke-Add $root $prefix }
    }
}
finally {
    Pop-Location
}
