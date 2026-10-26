$ErrorActionPreference = "Stop"
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$Failed = $false
$Extensions = @(".cpp", ".h", ".js", ".css", ".html", ".sh", ".ps1", ".cmd", ".py")
$Files = Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
    $Extensions -contains $_.Extension.ToLowerInvariant() -and
    $_.FullName -notmatch '[\\/](web[\\/]js[\\/]vendor|\.git|build|\.cache)[\\/]'
}
foreach ($File in $Files) {
    $Lines = 0
    $Reader = [System.IO.File]::OpenText($File.FullName)
    try {
        while ($null -ne $Reader.ReadLine()) { $Lines++ }
    } finally {
        $Reader.Dispose()
    }
    if ($Lines -gt 1000) {
        $Relative = $File.FullName.Substring($Root.Length).TrimStart('\', '/')
        Write-Error "[NAGGER] $Relative has $Lines lines. Split it, you absolute landfill."
        $Failed = $true
    }
}
if (-not $Failed) {
    Write-Host "[nagger] All first-party files are <= 1000 lines. Readability survives."
    exit 0
}
exit 1
