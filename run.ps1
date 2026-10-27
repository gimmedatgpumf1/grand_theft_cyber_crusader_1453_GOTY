param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ClientArguments
)

$root = $PSScriptRoot
$port = 1453

function Test-ServerPort {
    param([int] $Port)
    try {
        $socket = New-Object System.Net.Sockets.TcpClient
        $socket.Connect('127.0.0.1', $Port)
        $socket.Close()
        return $true
    } catch {
        return $false
    }
}

$server = $null
$exitCode = 1
try {
    $serverRunning = Test-ServerPort $port
    if (-not $serverRunning) {
        $serverBuild = Join-Path $root 'build.cmd'
        & $env:ComSpec /d /c "`"$serverBuild`""
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $nativeBuild = Join-Path $root 'native\build.cmd'
    & $env:ComSpec /d /c "`"$nativeBuild`""
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if (-not $serverRunning) {
        $serverPath = Join-Path $root 'build\bin\gtc1453.exe'
        $webRoot = Join-Path $root 'web'
        $server = Start-Process -FilePath $serverPath -WorkingDirectory $root `
            -ArgumentList @('--port', "$port", '--web', "`"$webRoot`"") `
            -WindowStyle Minimized -PassThru
        $ready = $false
        for ($attempt = 0; $attempt -lt 50; $attempt++) {
            if (Test-ServerPort $port) {
                $ready = $true
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $ready) { throw "The authoritative server did not open port $port." }
    }

    $clientPath = Join-Path $root 'build\bin\gtc-native-client.exe'
    if ($null -eq $ClientArguments -or $ClientArguments.Count -eq 0) {
        $client = Start-Process -FilePath $clientPath -WorkingDirectory $root -PassThru
    } else {
        $client = Start-Process -FilePath $clientPath -WorkingDirectory $root `
            -ArgumentList $ClientArguments -PassThru
    }
    $client.WaitForExit()
    $exitCode = $client.ExitCode
} finally {
    if ($null -ne $server -and -not $server.HasExited) {
        $server.Kill()
        $server.WaitForExit()
    }
}
exit $exitCode
