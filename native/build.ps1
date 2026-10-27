param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $BuildArguments
)

& (Join-Path $PSScriptRoot 'build.cmd') @BuildArguments
exit $LASTEXITCODE
