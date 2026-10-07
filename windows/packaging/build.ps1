[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [string]$Python,
    [string]$CMake,
    [string]$Git = 'git',
    [switch]$SkipTests,
    [switch]$SkipResources,
    [switch]$SkipPackage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$windowsRoot = Join-Path $repository 'windows'
$buildRoot = Join-Path $windowsRoot 'build\x64'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

if (-not $Python) {
    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -ne $pythonCommand -and $pythonCommand.Source -notmatch '\\WindowsApps\\') {
        $Python = $pythonCommand.Source
    } else {
        # Codex's bundled interpreter is optional; normal users can pass -Python.
        $bundledPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
        if (Test-Path -LiteralPath $bundledPython -PathType Leaf) { $Python = $bundledPython }
    }
}
if (-not $Python -or -not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    throw 'Python 3.10+ is required for lossless staging and isolated tests. Pass -Python with its executable path.'
}
$pythonVersion = & $Python -c 'import sys; print(sys.version.split()[0]); sys.exit(0 if sys.version_info >= (3, 10) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.10+ is required.' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer/vswhere is unavailable. Install Visual Studio Build Tools with Desktop development with C++, a Windows SDK and C++ CMake tools.'
}
$instancesJson = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio toolchain discovery failed.' }
$instances = @(($instancesJson -join "`n") | ConvertFrom-Json)
if ($instances.Count -eq 0) {
    throw 'No Visual Studio C++ x64 toolchain found. Add Desktop development with C++, a Windows SDK and C++ CMake tools.'
}
$instance = $instances[0]
$visualStudioPath = $instance.installationPath
if (-not $CMake) {
    $bundledCMake = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $bundledCMake -PathType Leaf) {
        $CMake = $bundledCMake
    } else {
        $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if ($null -ne $cmakeCommand) { $CMake = $cmakeCommand.Source }
    }
}
if (-not $CMake -or -not (Test-Path -LiteralPath $CMake -PathType Leaf)) {
    throw 'CMake is unavailable. Add the Visual Studio C++ CMake tools component or pass -CMake.'
}
$cmakeVersion = @(& $CMake --version)[0]
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect CMake.' }
$majorVersion = ([version]$instance.installationVersion).Major
$helpLines = @(& $CMake --help)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect CMake generators.' }
$generator = $null
foreach ($line in $helpLines) {
    if ($line -match "^\s*\*?\s*(Visual Studio $majorVersion [0-9]{4})\s*=") {
        $generator = $Matches[1]
        break
    }
}
if (-not $generator) {
    throw "This CMake does not support the installed Visual Studio major version $majorVersion. Update its CMake tools or pass a compatible -CMake."
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
Invoke-Checked $CMake @('-S', $windowsRoot, '-B', $buildRoot, '-G', $generator, '-A', 'x64', "-DCMAKE_GENERATOR_INSTANCE=$visualStudioPath", '-DBUILD_TESTING=ON')
Invoke-Checked $CMake @('--build', $buildRoot, '--config', $Configuration, '--parallel')

$executable = Join-Path $buildRoot "$Configuration\EndfieldHUDWindows.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Expected native executable is missing: $executable"
}
$resources = Join-Path (Split-Path -Parent $executable) 'Resources'
if (-not $SkipResources) {
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'stage_resources.py'), 'stage', '--repository', $repository, '--destination', $resources)
}
if (-not $SkipTests) {
    $ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw 'CTest must accompany CMake.' }
    Invoke-Checked $ctest @('--test-dir', $buildRoot, '-C', $Configuration, '--output-on-failure')
    Invoke-Checked $Python @('-m', 'unittest', 'discover', '-s', (Join-Path $windowsRoot 'tests'), '-p', 'test_*.py', '-v')
}

$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
$sdk = @($cache | Select-String '^CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION:.*=')
$sdkVersion = if ($sdk.Count -gt 0) { (($sdk[0].Line -split '=', 2)[1]) } else { $null }
if (-not $sdkVersion) {
    $appProject = Join-Path $buildRoot 'EndfieldHUDWindows.vcxproj'
    if (Test-Path -LiteralPath $appProject -PathType Leaf) {
        [xml]$projectXml = Get-Content -LiteralPath $appProject -Raw
        $sdkNodes = @($projectXml.GetElementsByTagName('WindowsTargetPlatformVersion'))
        if ($sdkNodes.Count -gt 0) { $sdkVersion = $sdkNodes[0].InnerText }
    }
}
$msvcDefault = Join-Path $visualStudioPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
$compilerVersion = if (Test-Path -LiteralPath $msvcDefault) { (Get-Content -LiteralPath $msvcDefault -Raw).Trim() } else { 'unverified' }
$buildMetadata = Join-Path $buildRoot 'build-metadata.json'
[ordered]@{
    schema = 1
    architecture = 'x64'
    configuration = $Configuration
    cmake = $cmakeVersion
    generator = $generator
    visual_studio_version = $instance.installationVersion
    msvc_tools_version = $compilerVersion
    windows_sdk = $(if ($sdkVersion) { $sdkVersion } else { 'unverified' })
    runtime = '/MT for Release; /MTd for Debug'
    python = $pythonVersion
    native_tests = $(if ($SkipTests) { 'skipped' } else { 'passed' })
    resource_verification = $(if ($SkipResources) { 'skipped' } else { 'passed' })
    stage = 'feasibility developer preview; Windows parity remains unverified'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $buildMetadata -Encoding UTF8

if (-not $SkipPackage) {
    if ($SkipResources) { throw 'Packaging requires verified staged resources; remove -SkipResources or also pass -SkipPackage.' }
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'package.py'), '--format', 'portable', '--repository', $repository, '--executable', $executable, '--resources', $resources, '--build-metadata', $buildMetadata, '--git', $Git)
}
Write-Output "Windows $Configuration executable: $executable"
Write-Output 'Consumer release is gated; no version, release publication or Mac update feed was changed.'
