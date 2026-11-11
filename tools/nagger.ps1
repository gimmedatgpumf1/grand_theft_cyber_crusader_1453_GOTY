# The line guard.
#
# A file has three states here, and it says so every single time:
#
#   under 750   left alone.
#   750 - 1000  nagged. Loudly. Repeatedly. With a bar showing exactly how close
#               the file is to the wall and a reminder of what happens there.
#   1000+       the build stops. Not a warning, not a note in a log: the compiler
#               is never reached, and nothing is built until the file is split.
#
# The wall is a wall. Splitting a file is cheap; a thousand-line file is not.
$ErrorActionPreference = "Stop"
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$HardLimit = 1000
$WarnLimit = 750
$Failed = $false
$Extensions = @(".cpp", ".h", ".js", ".css", ".html", ".sh", ".ps1", ".cmd", ".py")
$Files = Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
    $Extensions -contains $_.Extension.ToLowerInvariant() -and
    $_.FullName -notmatch '[\\/](web[\\/]js[\\/]vendor|native[\\/]vendor|\.git|build|\.cache)[\\/]'
}

# A twenty-cell bar of how much of the wall is already used.
function Get-Bar([int]$Lines) {
    $Filled = [int][Math]::Floor($Lines * 20 / $HardLimit)
    if ($Filled -gt 20) { $Filled = 20 }
    return ("#" * $Filled) + ("-" * (20 - $Filled))
}

$Warned = @()
$Blocked = @()
foreach ($File in $Files) {
    $Lines = 0
    $Reader = [System.IO.File]::OpenText($File.FullName)
    try {
        while ($null -ne $Reader.ReadLine()) { $Lines++ }
    } finally {
        $Reader.Dispose()
    }
    $Relative = $File.FullName.Substring($Root.Length).TrimStart('\', '/')
    if ($Lines -gt $HardLimit) {
        Write-Host "[NAGGER] ################################################################" -ForegroundColor Red
        Write-Host "[NAGGER]  BLOCKED: $Relative is $Lines lines." -ForegroundColor Red
        Write-Host "[NAGGER]  $HardLimit is the wall. You did not hit it, you drove through it." -ForegroundColor Red
        Write-Host "[NAGGER]  I will not compile a single object until you clean this up." -ForegroundColor Red
        Write-Host "[NAGGER]  Not one. Go split it, you absolute landfill." -ForegroundColor Red
        Write-Host "[NAGGER] ################################################################" -ForegroundColor Red
        $Blocked += "$Relative ($Lines)"
        $Failed = $true
    } elseif ($Lines -ge $WarnLimit) {
        $Left = $HardLimit - $Lines
        $Warned += "$Relative ($Lines)"
        Write-Host "[NAGGER] [$(Get-Bar $Lines)] $Relative" -ForegroundColor Yellow
        Write-Host "[NAGGER]   $Lines lines. $Left left before I spit the dummy and stop building." -ForegroundColor Yellow
        Write-Host "[NAGGER]   Time to split or I will spit! 750 is where a file stops being" -ForegroundColor Yellow
        Write-Host "[NAGGER]   code and starts being a landfill with a filename." -ForegroundColor Yellow
        if ($Left -le 0) {
            Write-Host "[NAGGER]   AT THE WALL. Not near it, on it. The very next line stops the" -ForegroundColor Red
            Write-Host "[NAGGER]   build. Split it, you absolute landfill." -ForegroundColor Red
        } elseif ($Left -le 50) {
            Write-Host "[NAGGER]   $Left LINES FROM THE WALL. This thing is one sneeze away from" -ForegroundColor Red
            Write-Host "[NAGGER]   being a build failure. Split it NOW, you digital hoarder." -ForegroundColor Red
        } elseif ($Left -le 150) {
            Write-Host "[NAGGER]   $Left lines of headroom, which is not headroom, it is a countdown." -ForegroundColor Yellow
        } else {
            Write-Host "[NAGGER]   Every line you add from here is another brick in your own wall." -ForegroundColor Yellow
        }
    }
}

if ($Warned.Count -gt 0 -and -not $Failed) {
    Write-Host "[nagger] ################################################################"
    Write-Host "[nagger]  $($Warned.Count) file(s) in the danger zone. I am watching all of them."
    foreach ($Entry in $Warned) {
        Write-Host "[nagger]   - $Entry"
    }
    Write-Host "[nagger]  Split one of them and I will shut up about it. Build continues,"
    Write-Host "[nagger]  but I am not happy about it, and neither is the next person."
    Write-Host "[nagger] ################################################################"
}
if ($Failed) {
    Write-Host "[nagger] $($Blocked.Count) file(s) past the hard limit: $($Blocked -join ', ')" -ForegroundColor Red
    Write-Host "[nagger] Nothing was compiled. Not one object. Split the file(s) above and" -ForegroundColor Red
    Write-Host "[nagger] try again -- and do not come back until you have." -ForegroundColor Red
    exit 1
}
if ($Warned.Count -eq 0) {
    Write-Host "[nagger] All first-party files are under 750 lines. Readability survives."
}
Write-Host "[nagger] Build successful? Push to GitHub!"
Write-Host "[nagger] Less yapping, more coding! Don't bother the King until you have fixed it!"
exit 0
