$ErrorActionPreference = "Stop"
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$Failed = $false
$Extensions = @(".cpp", ".h", ".js", ".css", ".html", ".sh", ".ps1", ".cmd", ".py")
$Files = Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
    $Extensions -contains $_.Extension.ToLowerInvariant() -and
    $_.FullName -notmatch '[\\/](web[\\/]js[\\/]vendor|native[\\/]vendor|\.git|build|\.cache)[\\/]'
}
foreach ($File in $Files) {
    $Lines = 0
    $Reader = [System.IO.File]::OpenText($File.FullName)
    try {
        while ($null -ne $Reader.ReadLine()) { $Lines++ }
    } finally {
        $Reader.Dispose()
    }
    $Relative = $File.FullName.Substring($Root.Length).TrimStart('\', '/')
    if ($Lines -gt 1000) {
        Write-Error "[NAGGER] $Relative has $Lines lines. Split it, you absolute landfill."
        $Failed = $true
    } elseif ($Lines -ge 750) {
        Write-Host "[NAGGER] $Relative has $Lines lines. Time to split or I will spit!" -ForegroundColor Yellow
    }
}
if (-not $Failed) {
    Write-Host "[nagger] All first-party files are <= 1000 lines. Readability survives."
}
Write-Host "[nagger] Build successful? Push to GitHub!"
Write-Host "[nagger] Less yapping, more coding! Don't bother the King until you have fixed it!"
if ($Failed) { exit 1 }
exit 0
