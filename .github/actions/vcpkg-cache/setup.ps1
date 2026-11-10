$ErrorActionPreference = 'Stop'

if ($IsWindows)
{
    & ./gnb.bat setup --vcpkg-only
    $vcpkgExe = Join-Path $PWD '.vcpkg/vcpkg.exe'
}
else
{
    & ./gnb.sh setup --vcpkg-only
    $vcpkgExe = Join-Path $PWD '.vcpkg/vcpkg'
}
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }

$nuget = & $vcpkgExe fetch nuget | Select-Object -Last 1
if ($LASTEXITCODE -ne 0 -or !$nuget -or !(Test-Path $nuget))
{
    throw 'Failed to acquire NuGet for the vcpkg binary cache'
}

function Invoke-NuGet
{
    if ($IsWindows)
    {
        & $nuget @args
    }
    else
    {
        & mono $nuget @args
    }
    if ($LASTEXITCODE -ne 0) { throw 'NuGet cache configuration failed' }
}

Invoke-NuGet sources add -Name 'gk-vcpkg-github' -Source $env:FEED_URL `
    -UserName $env:NUGET_USER -Password $env:NUGET_TOKEN -StorePasswordInClearText
$access = if ($env:IS_FORK -eq 'true') { 'read' } else { 'readwrite' }
if ($access -eq 'readwrite')
{
    Invoke-NuGet setapikey $env:NUGET_TOKEN -Source $env:FEED_URL
}
"VCPKG_BINARY_SOURCES=clear;nuget,$($env:FEED_URL),$access" >> $env:GITHUB_ENV
"VCPKG_NUGET_REPOSITORY=$($env:REPOSITORY_URL)" >> $env:GITHUB_ENV
Write-Host "Configured GitHub Packages vcpkg binary cache ($access)"
