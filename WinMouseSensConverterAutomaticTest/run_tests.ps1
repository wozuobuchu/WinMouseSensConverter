[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$NoBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot

if (-not $NoBuild) {
    & (Join-Path $repositoryRoot "build_windows.ps1") -Configuration $Configuration -Platform x64 -NoRestore
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$testExecutable = Join-Path $repositoryRoot "x64\$Configuration\WinMouseSensConverterAutomaticTest.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw "The automatic test executable was not found at '$testExecutable'. Build the $Configuration x64 configuration first."
}

& $testExecutable
exit $LASTEXITCODE
