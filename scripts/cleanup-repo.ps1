<#
.SYNOPSIS
    Removes dead weight from the DeskNX repository, in explicit groups.

.DESCRIPTION
    The GitHub connector used to prepare this cleanup cannot delete files, so the
    deletions live here as a reviewable, idempotent script instead of an opaque
    one-off. See docs/CLEANUP.md for the evidence behind every path.

    Dry-run by default. Nothing is removed until -Apply is passed.

.PARAMETER Group
    safe    - build artifacts, dead code, duplicate lockfile, unreferenced docs (default).
    romfs   - the duplicated RomFS mount layouts. Only after a console smoke test:
              one of the four paths may be the only one that works on some firmware.
    all     - safe + romfs.

.PARAMETER Apply
    Actually delete. Without it the script only reports.

.EXAMPLE
    pwsh scripts/cleanup-repo.ps1
    pwsh scripts/cleanup-repo.ps1 -Apply -Group safe
#>
[CmdletBinding()]
param(
    [ValidateSet('safe', 'romfs', 'all')]
    [string]$Group = 'safe',

    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repositoryRoot
try {
    if (-not (Test-Path (Join-Path $repositoryRoot '.git'))) {
        throw "Not a git repository: $repositoryRoot"
    }

    $groups = [ordered]@{
        safe  = @(
            'desknx-release.zip'
            '.vite'
            'metadata.json'
            'src/services/switchHardwareService.ts'
            'bun.lock'
            'docs/STAGE_1_SMOKE_TEST.md'
            'docs/SWITCH_HARDWARE_PORTING.md'
        )
        romfs = @(
            'romfs/html-document/assets'
            'romfs/html-document/.htdocs'
        )
    }

    $targets = switch ($Group) {
        'safe'  { $groups.safe }
        'romfs' { $groups.romfs }
        'all'   { $groups.safe + $groups.romfs }
    }

    $modeLabel = if ($Apply) { 'APPLY' } else { 'dry run' }
    Write-Host ("DeskNX cleanup - group '{0}' - {1}" -f $Group, $modeLabel) -ForegroundColor Cyan
    Write-Host ('-' * 68)

    $removed = 0
    $skipped = 0

    foreach ($relativePath in $targets) {
        $absolutePath = Join-Path $repositoryRoot $relativePath
        $tracked = git ls-files -- $relativePath

        if (-not $tracked -and -not (Test-Path $absolutePath)) {
            Write-Host ("  skip   {0} (not tracked, not on disk)" -f $relativePath) -ForegroundColor DarkGray
            $skipped++
            continue
        }

        if (-not $Apply) {
            $fileCount = if (-not $tracked) { 0 } else { @($tracked).Count }
            Write-Host ("  remove {0} ({1} tracked file(s))" -f $relativePath, $fileCount) -ForegroundColor Yellow
            $removed++
            continue
        }

        if ($tracked) {
            git rm -r --quiet --ignore-unmatch -- $relativePath | Out-Null
        }
        if (Test-Path $absolutePath) {
            Remove-Item -Recurse -Force -LiteralPath $absolutePath
        }
        Write-Host ("  removed {0}" -f $relativePath) -ForegroundColor Green
        $removed++
    }

    Write-Host ('-' * 68)
    if (-not $Apply) {
        Write-Host "Would remove $removed path(s), skip $skipped. Re-run with -Apply to do it." -ForegroundColor Cyan
    }
    else {
        Write-Host "Removed $removed path(s), skipped $skipped." -ForegroundColor Green
        Write-Host @"

Next steps:
  1. Review the staged deletions:      git status --short
  2. Commit:                           git commit -m "chore: drop build artifacts, dead code and duplicate lockfile"
  3. Push:                             git push
  4. Keep it clean: .gitignore already covers .vite/, *.zip, dist/, build/, SdOut/
     (if romfs was deleted, add 'romfs/' once build.yml runs prepare-romfs.js)
"@
    }
}
finally {
    Pop-Location
}

{"content":"<#\n.SYNOPSIS\n    Removes dead weight from the DeskNX repository, in explicit groups.\n\n.DESCRIPTION\n    The GitHub connector used to prepare this cleanup cannot delete files, so the\n    deletions live here as a reviewable, idempotent script instead of an opaque\n    one-off. See docs/CLEANUP.md for the evidence behind every path.\n\n    Dry-run by default. Nothing is removed until -Apply is passed.\n\n.PARAMETER Group\n    safe    - build artifacts, dead code, duplicate lockfile, unreferenced docs (default).\n    romfs   - the duplicated RomFS mount layouts. Only after a console smoke test:\n              one of the four paths may be the only one that works on some firmware.\n    all     - safe + romfs.\n\n.PARAMETER Apply\n    Actually delete. Without it the script only reports.\n\n.EXAMPLE\n    pwsh scripts/cleanup-repo.ps1\n    pwsh scripts/cleanup-repo.ps1 -Apply -Group safe\n#>\n[CmdletBinding()]\nparam(\n    [ValidateSet('safe', 'romfs', 'all')]\n    [string]$Group = 'safe',\n\n    [switch]$Apply\n)\n\n$ErrorActionPreference = 'Stop'\n\n$repositoryRoot = Split-Path -Parent $PSScriptRoot\nPush-Location $repositoryRoot\ntry {\n    if (-not (Test-Path (Join-Path $repositoryRoot '.git'))) {\n        throw \"Not a git repository: $repositoryRoot\"\n    }\n\n    $groups = [ordered]@{\n        safe  = @(\n            'desknx-release.zip'\n            '.vite'\n            'metadata.json'\n            'src/services/switchHardwareService.ts'\n            'bun.lock'\n            'docs/STAGE_1_SMOKE_TEST.md'\n            'docs/SWITCH_HARDWARE_PORTING.md'\n        )\n        romfs = @(\n            'romfs/html-document/assets'\n            'romfs/html-document/.htdocs'\n        )\n    }\n\n    $targets = switch ($Group) {\n        'safe'  { $groups.safe }\n        'romfs' { $groups.romfs }\n        'all'   { $groups.safe + $groups.romfs }\n    }\n\n    $modeLabel = if ($Apply) { 'APPLY' } else { 'dry run' }\n    Write-Host (\"DeskNX cleanup - group '{0}' - {1}\" -f $Group, $modeLabel) -ForegroundColor Cyan\n    Write-Host ('-' * 68)\n\n    $removed = 0\n    $skipped = 0\n\n    foreach ($relativePath in $targets) {\n        $absolutePath = Join-Path $repositoryRoot $relativePath\n        $tracked = git ls-files -- $relativePath\n\n        if (-not $tracked -and -not (Test-Path $absolutePath)) {\n            Write-Host (\"  skip   {0} (not tracked, not on disk)\" -f $relativePath) -ForegroundColor DarkGray\n            $skipped++\n            continue\n        }\n\n        if (-not $Apply) {\n            $fileCount = if (-not $tracked) { 0 } else { @($tracked).Count }\n            Write-Host (\"  remove {0} ({1} tracked file(s))\" -f $relativePath, $fileCount) -ForegroundColor Yellow\n            $removed++\n            continue\n        }\n\n        if ($tracked) {\n            git rm -r --quiet --ignore-unmatch -- $relativePath | Out-Null\n        }\n        if (Test-Path $absolutePath) {\n            Remove-Item -Recurse -Force -LiteralPath $absolutePath\n        }\n        Write-Host (\"  removed {0}\" -f $relativePath) -ForegroundColor Green\n        $removed++\n    }\n\n    Write-Host ('-' * 68)\n    if (-not $Apply) {\n        Write-Host \"Would remove $removed path(s), skip $skipped. Re-run with -Apply to do it.\" -ForegroundColor Cyan\n    }\n    else {\n        Write-Host \"Removed $removed path(s), skipped $skipped.\" -ForegroundColor Green\n        Write-Host @\"\n\nNext steps:\n  1. Review the staged deletions:      git status --short\n  2. Commit:                           git commit -m \"chore: drop build artifacts, dead code and duplicate lockfile\"\n  3. Push:                             git push\n  4. Keep it clean: .gitignore already covers .vite/, *.zip, dist/, build/, SdOut/\n     (if romfs was deleted, add 'romfs/' once build.yml runs prepare-romfs.js)\n\"@\n    }\n}\nfinally {\n    Pop-Location\n}\n"}