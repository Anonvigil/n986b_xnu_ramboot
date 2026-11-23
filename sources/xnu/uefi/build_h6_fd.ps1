param(
    [Parameter(Mandatory = $true)][string]$Tree,
    [Parameter(Mandatory = $true)][string]$LlvmBin,
    [Parameter(Mandatory = $true)][string]$MakeExe,
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
$firmware = (Resolve-Path -LiteralPath $Tree).Path
$llvm = (Resolve-Path -LiteralPath $LlvmBin).Path
$make = (Resolve-Path -LiteralPath $MakeExe).Path
$baseTools = Join-Path $firmware 'Common\edk2\BaseTools'
$conf = Join-Path $firmware 'work\edk2-conf'
$hostBin = Join-Path $firmware 'work\hostbin'
$baseToolBin = Join-Path $baseTools 'Source\C\bin'
if (-not (Test-Path -LiteralPath (Join-Path $baseToolBin 'GenFv.exe') -PathType Leaf)) {
    $baseToolBin = Join-Path $baseTools 'Bin\Win32'
}

foreach ($required in @(
    (Join-Path $llvm 'clang.exe'),
    (Join-Path $llvm 'llvm-ar.exe'),
    (Join-Path $baseTools 'Source\Python\build\build.py'),
    (Join-Path $baseToolBin 'GenFv.exe'),
    (Join-Path $conf 'tools_def.txt'),
    (Join-Path $firmware 'Platform\Samsung\exynos990\Research\Ios165RoStoreH1Branch\Loader\N986BXnuLoader.inf')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing build input: $required"
    }
}

New-Item -ItemType Directory -Path $hostBin -Force | Out-Null
Copy-Item -LiteralPath $make -Destination (Join-Path $hostBin 'make.exe') -Force
$env:WORKSPACE = $firmware
$env:PACKAGES_PATH = @(
    $firmware,
    (Join-Path $firmware 'Common\edk2'),
    (Join-Path $firmware 'Common\edk2-platforms'),
    (Join-Path $firmware 'GPLDrivers\Library\SimpleInit')
) -join ';'
$env:EDK_TOOLS_PATH = $baseTools
$env:BASE_TOOLS_PATH = $baseTools
$env:CONF_PATH = $conf
$env:PYTHONPATH = Join-Path $baseTools 'Source\Python'
$env:PYTHON_COMMAND = 'python'
$env:CLANG38_BIN = $llvm + '\'
$env:CLANG38_AARCH64_PREFIX = $llvm + '\llvm-'
$env:GCC_HOST_BIN = $hostBin + '\'
$env:PATH = @(
    $hostBin,
    (Join-Path $baseTools 'BinWrappers\WindowsLike'),
    $llvm,
    $baseToolBin,
    $env:PATH
) -join ';'

$buildArgs = @(
    (Join-Path $baseTools 'Source\Python\build\build.py'),
    '-p', 'Platform\Samsung\exynos990\Research\Ios165RoStoreH1Branch\c2s-ios165-ro-store-h1-branch-host.dsc',
    '-a', 'AARCH64', '-b', 'DEBUG', '-t', 'CLANG38', '-n', "$Jobs",
    '-D', 'FIRMWARE_VER=c000354', '-D', 'USE_UART=0', '-D', 'FIX_CLANG=0',
    '-D', 'NO_EXCEPTION_DISPLAY=0', '-D', 'FD_BASE=0x90000000',
    '-D', 'FD_SIZE=0x00700000', '-D', 'ENABLE_LINUX_UTILS=1'
)
Push-Location -LiteralPath $firmware
try {
    & python @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "EDK II build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$fd = Join-Path $firmware 'Build\c2s-ios165-ro-store-h1-branch-host\DEBUG_CLANG38\FV\EXYNOS990_UEFI.fd'
if (-not (Test-Path -LiteralPath $fd -PathType Leaf)) { throw "FD was not produced: $fd" }
$actual = (Get-FileHash -LiteralPath $fd -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Built FD: $fd"
Write-Host "SHA-256: $actual"
Write-Host 'Compare with the recorded H6 FD SHA-256 in source-manifest.json. A different hash is an untested build.'
