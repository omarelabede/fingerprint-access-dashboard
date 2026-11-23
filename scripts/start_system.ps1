[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^COM[1-9][0-9]*$")]
    [string]$Port,

    [ValidateRange(1, 4000000)]
    [int]$Baud = 9600,

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

function Test-LocalTcpPort {
    param(
        [Parameter(Mandatory = $true)]
        [int]$TcpPort
    )

    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connectionTask = $client.ConnectAsync("127.0.0.1", $TcpPort)
        if (-not $connectionTask.Wait(250)) {
            return $false
        }
        return $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Wait-ForDashboard {
    param(
        [Parameter(Mandatory = $true)]
        [Diagnostics.Process]$Process,

        [int]$TimeoutSeconds = 10
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            throw "The dashboard stopped before it became ready. Run start_dashboard.ps1 to see its output."
        }
        if (Test-LocalTcpPort -TcpPort 5000) {
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw "The dashboard did not become ready on 127.0.0.1:5000 within $TimeoutSeconds seconds."
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$venvPython = Join-Path $projectRoot ".venv\Scripts\python.exe"
$dashboardScript = Join-Path $projectRoot "host\dashboard.py"
$loggerScript = Join-Path $projectRoot "host\access_logger.py"

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

$availablePorts = @([IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
if ($availablePorts -notcontains $Port) {
    $availablePortText = if ($availablePorts.Count -gt 0) {
        $availablePorts -join ", "
    }
    else {
        "none"
    }
    throw "Serial port '$Port' is not available. Detected ports: $availablePortText. Connect the Arduino and check Device Manager."
}

if (Test-LocalTcpPort -TcpPort 5000) {
    throw "Port 5000 is already in use. Stop the existing dashboard or service, then run this script again."
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
$dashboardProcess = $null

try {
    $env:DASHBOARD_USERNAME = $Username
    $env:DASHBOARD_PASSWORD = $plainPassword

    $dashboardArguments = @(
        ('"{0}"' -f $dashboardScript),
        "--database",
        ('"{0}"' -f $databasePath)
    )
    $dashboardProcess = Start-Process `
        -FilePath $venvPython `
        -ArgumentList $dashboardArguments `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden `
        -PassThru

    Wait-ForDashboard -Process $dashboardProcess

    Write-Host "The complete system is running."
    Write-Host "Shared database: $databasePath"
    Write-Host "Arduino serial: $Port at $Baud baud"
    Write-Host "Arabic:  http://127.0.0.1:5000/ar"
    Write-Host "English: http://127.0.0.1:5000/en"
    Write-Host "The logger is in this window. Press Ctrl+C to stop both processes."

    & $venvPython $loggerScript --port $Port --baud $Baud --database $databasePath
    if ($LASTEXITCODE -ne 0) {
        throw "The serial logger exited with code $LASTEXITCODE."
    }
}
finally {
    if ($null -ne $dashboardProcess -and -not $dashboardProcess.HasExited) {
        Stop-Process -Id $dashboardProcess.Id -ErrorAction SilentlyContinue
        $dashboardProcess.WaitForExit(3000) | Out-Null
    }

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
