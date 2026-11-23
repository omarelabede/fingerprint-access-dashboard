[CmdletBinding()]
param(
    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList,

        [Parameter(Mandatory = $true)]
        [string]$FailureMessage
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit code $LASTEXITCODE)."
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$venvDirectory = Join-Path $projectRoot ".venv"
$venvPython = Join-Path $venvDirectory "Scripts\python.exe"
$requirementsPath = Join-Path $projectRoot "requirements.txt"
$databasePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "access_control.db"))

if (-not (Test-Path -LiteralPath $requirementsPath -PathType Leaf)) {
    throw "requirements.txt was not found at: $requirementsPath"
}

if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    $bootstrapCommand = $null
    $bootstrapArguments = @()
    $bootstrapVersion = $null
    $candidates = @(
        @{ Command = "py"; Arguments = @("-3") },
        @{ Command = "python"; Arguments = @() }
    )

    foreach ($candidate in $candidates) {
        $commandInfo = Get-Command -Name $candidate.Command -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -eq $commandInfo) {
            continue
        }

        $versionArguments = @($candidate.Arguments) + @(
            "-c",
            "import sys; print('.'.join(map(str, sys.version_info[:3])))"
        )
        $savedErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $versionText = & $commandInfo.Source @versionArguments 2>$null
            $candidateExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
        if ($candidateExitCode -ne 0) {
            continue
        }

        try {
            $parsedVersion = [Version]($versionText | Select-Object -Last 1)
        }
        catch {
            continue
        }

        if ($parsedVersion -ge [Version]"3.10") {
            $bootstrapCommand = $commandInfo.Source
            $bootstrapArguments = @($candidate.Arguments)
            $bootstrapVersion = $parsedVersion
            break
        }
    }

    if ($null -eq $bootstrapCommand) {
        throw "Python 3.10 or newer was not found. Install Python from python.org, enable the Python Launcher or Add Python to PATH, then run this script again."
    }

    Write-Host "Creating .venv with Python $bootstrapVersion ..."
    $createVenvArguments = $bootstrapArguments + @("-m", "venv", $venvDirectory)
    Invoke-CheckedCommand -FilePath $bootstrapCommand `
        -ArgumentList $createVenvArguments `
        -FailureMessage "Unable to create the virtual environment"
}

$venvVersionText = & $venvPython -c "import sys; print('.'.join(map(str, sys.version_info[:3])))"
if ($LASTEXITCODE -ne 0) {
    throw "The virtual-environment Python cannot run. Remove .venv and run setup.ps1 again."
}
$venvVersion = [Version]($venvVersionText | Select-Object -Last 1)
if ($venvVersion -lt [Version]"3.10") {
    throw ".venv uses Python $venvVersion; Python 3.10 or newer is required. Remove .venv and run setup.ps1 again."
}

Write-Host "Installing Python dependencies ..."
Invoke-CheckedCommand -FilePath $venvPython `
    -ArgumentList @("-m", "pip", "install", "--upgrade", "pip") `
    -FailureMessage "Unable to update pip"
Invoke-CheckedCommand -FilePath $venvPython `
    -ArgumentList @("-m", "pip", "install", "-r", $requirementsPath) `
    -FailureMessage "Unable to install project dependencies"

Write-Host "Preparing the shared SQLite database: $databasePath"
Invoke-CheckedCommand -FilePath $venvPython `
    -ArgumentList @((Join-Path $projectRoot "host\init_database.py"), "--database", $databasePath) `
    -FailureMessage "Unable to initialize the SQLite database"

if (-not $SkipTests -and (Test-Path -LiteralPath (Join-Path $projectRoot "tests") -PathType Container)) {
    Write-Host "Running the automated checks ..."
    Push-Location $projectRoot
    try {
        Invoke-CheckedCommand -FilePath $venvPython `
            -ArgumentList @("-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v") `
            -FailureMessage "One or more automated checks failed"
    }
    finally {
        Pop-Location
    }
}
elseif (-not $SkipTests) {
    Write-Warning "No tests/ directory is included in this source package; automated checks were skipped."
}

Write-Host "Setup completed successfully."
Write-Host "Next: .\scripts\start_system.ps1 -Port COMx"


