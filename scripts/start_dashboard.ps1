[CmdletBinding()]
param(
    [string]$Username,
    [string]$Database = "access_control.db"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function ConvertFrom-TemporarySecureString {
    param(
        [Parameter(Mandatory = $true)]
        [Security.SecureString]$SecureValue
    )

    $pointer = [IntPtr]::Zero
    try {
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureValue)
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    }
    finally {
        if ($pointer -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
        }
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$venvPython = Join-Path $projectRoot ".venv\Scripts\python.exe"
$dashboardScript = Join-Path $projectRoot "host\dashboard.py"

if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    throw ".venv Python was not found. Run .\scripts\setup.ps1 first."
}

if ([System.IO.Path]::IsPathRooted($Database)) {
    $databasePath = [System.IO.Path]::GetFullPath($Database)
}
else {
    $databasePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Database))
}
if (-not (Test-Path -LiteralPath $databasePath -PathType Leaf)) {
    throw "The database was not found at '$databasePath'. Run .\scripts\setup.ps1 first."
}

if ([string]::IsNullOrWhiteSpace($Username)) {
    $Username = Read-Host "Dashboard username"
}
$Username = $Username.Trim()
if ([string]::IsNullOrWhiteSpace($Username)) {
    throw "The dashboard username cannot be empty."
}

$securePassword = Read-Host "Dashboard password" -AsSecureString
if ($securePassword.Length -eq 0) {
    throw "The dashboard password cannot be empty."
}
$plainPassword = ConvertFrom-TemporarySecureString -SecureValue $securePassword

$hadPreviousUsername = Test-Path Env:DASHBOARD_USERNAME
$hadPreviousPassword = Test-Path Env:DASHBOARD_PASSWORD
$previousUsername = [Environment]::GetEnvironmentVariable("DASHBOARD_USERNAME", "Process")
$previousPassword = [Environment]::GetEnvironmentVariable("DASHBOARD_PASSWORD", "Process")

try {
    $env:DASHBOARD_USERNAME = $Username
    $env:DASHBOARD_PASSWORD = $plainPassword

    Write-Host "Dashboard database: $databasePath"
    Write-Host "Arabic:  http://127.0.0.1:5000/ar"
    Write-Host "English: http://127.0.0.1:5000/en"
    Write-Host "Press Ctrl+C to stop the dashboard."

    & $venvPython $dashboardScript --database $databasePath
    if ($LASTEXITCODE -ne 0) {
        throw "The dashboard exited with code $LASTEXITCODE."
    }
}
finally {
    if ($hadPreviousUsername) {
        $env:DASHBOARD_USERNAME = $previousUsername
    }
    else {
        Remove-Item Env:DASHBOARD_USERNAME -ErrorAction SilentlyContinue
    }
    if ($hadPreviousPassword) {
        $env:DASHBOARD_PASSWORD = $previousPassword
    }
    else {
        Remove-Item Env:DASHBOARD_PASSWORD -ErrorAction SilentlyContinue
    }

    $plainPassword = $null
    $securePassword.Dispose()
}
