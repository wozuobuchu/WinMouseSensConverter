[CmdletBinding()]
param(
    [string]$SolutionPath,
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$NoRestore,
    [ValidateSet("quiet", "minimal", "normal", "detailed", "diagnostic")]
    [string]$Verbosity = "minimal"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-ProcessPath {
    $pathVariables = @(
        [Environment]::GetEnvironmentVariables([EnvironmentVariableTarget]::Process).GetEnumerator() |
            Where-Object { $_.Key -ieq "Path" }
    )
    if ($pathVariables.Count -le 1) {
        return
    }

    # A process can inherit both PATH and Path. MSBuild's .NET Framework tool
    # launcher treats their names case-insensitively and fails on the duplicate.
    $segments = New-Object 'System.Collections.Generic.List[string]'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)

    foreach ($pathVariable in ($pathVariables | Sort-Object { $_.Value.Length } -Descending)) {
        foreach ($segment in ($pathVariable.Value -split ";")) {
            $trimmedSegment = $segment.Trim()
            if ($trimmedSegment -and $seen.Add($trimmedSegment)) {
                [void]$segments.Add($trimmedSegment)
            }
        }
    }

    foreach ($pathVariable in $pathVariables) {
        Remove-Item -LiteralPath "Env:$($pathVariable.Key)"
    }
    $env:Path = $segments -join ";"
}

function Resolve-SolutionPath {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $resolved = Resolve-Path -LiteralPath $RequestedPath -ErrorAction Stop
        return $resolved.ProviderPath
    }

    # -Include is ignored for a literal directory path by Windows PowerShell 5.1.
    $solutions = @(Get-ChildItem -LiteralPath $PSScriptRoot -File |
        Where-Object { $_.Extension -ieq ".sln" -or $_.Extension -ieq ".slnx" } |
        Sort-Object @{ Expression = { if ($_.Extension -ieq ".slnx") { 0 } else { 1 } } }, Name)

    if ($solutions.Count -eq 0) {
        throw "No .sln or .slnx file found in '$PSScriptRoot'. Pass -SolutionPath explicitly."
    }

    if ($solutions.Count -gt 1) {
        $names = ($solutions | ForEach-Object { "  - $($_.FullName)" }) -join [Environment]::NewLine
        throw "Multiple solution files found. Pass -SolutionPath explicitly:$([Environment]::NewLine)$names"
    }

    return $solutions[0].FullName
}

function Find-VsWhere {
    $command = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    if ($programFilesX86) {
        $candidate = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "vswhere.exe was not found. Install Visual Studio Installer or add vswhere.exe to PATH."
}

function Find-VisualStudio {
    $vsWhere = Find-VsWhere
    $installationPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

    if (-not $installationPath) {
        throw "No Visual Studio installation with MSBuild and MSVC x86/x64 tools was found."
    }

    $msBuild = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path -LiteralPath $msBuild)) {
        $msBuild = Join-Path $installationPath "MSBuild\15.0\Bin\MSBuild.exe"
    }
    if (-not (Test-Path -LiteralPath $msBuild)) {
        throw "MSBuild.exe was not found under '$installationPath'."
    }

    $vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat was not found under '$installationPath'."
    }

    [PSCustomObject]@{
        InstallationPath = $installationPath
        MSBuild          = $msBuild
        VsDevCmd         = $vsDevCmd
    }
}

function Invoke-MsBuildInVsEnvironment {
    param(
        [string]$VsDevCmd,
        [string]$MSBuild,
        [string]$Solution,
        [string]$TargetArch,
        [string[]]$MSBuildArguments
    )

    $commandParts = @(
        "call",
        ('"{0}"' -f $VsDevCmd),
        "-arch=$TargetArch",
        "-host_arch=x64",
        "&&",
        ('"{0}"' -f $MSBuild),
        ('"{0}"' -f $Solution)
    ) + $MSBuildArguments

    $commandLine = $commandParts -join " "

    & cmd.exe /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

Normalize-ProcessPath

$solution = Resolve-SolutionPath -RequestedPath $SolutionPath
$visualStudio = Find-VisualStudio

$targets = if ($Clean) { "Clean;Build" } else { "Build" }
$restoreProperty = if ($NoRestore) { "false" } else { "true" }

$msBuildArgs = @(
    "/m:1",
    "/nologo",
    "/v:$Verbosity",
    "/t:$targets",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:RestorePackages=$restoreProperty",
    "/p:RestoreProjectStyle=PackageReference"
)

Write-Host "Solution: $solution"
Write-Host "Visual Studio: $($visualStudio.InstallationPath)"
Write-Host "MSBuild: $($visualStudio.MSBuild)"
Write-Host "Configuration: $Configuration"
Write-Host "Platform: $Platform"

Invoke-MsBuildInVsEnvironment -VsDevCmd $visualStudio.VsDevCmd -MSBuild $visualStudio.MSBuild -Solution $solution -TargetArch $Platform -MSBuildArguments $msBuildArgs

Write-Host "Build completed successfully."
