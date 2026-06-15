# Neurus — Environment Setup Script
#
# Run this script after installing the prerequisites:
#   1. Vulkan SDK 1.4.x from https://vulkan.lunarg.com/ (install to D:\VulkanSDK)
#   2. Qt 6.8+ from https://www.qt.io/download (install to D:\Qt)
#
# Usage: .\setup.ps1

param(
    [string]$VulkanSdkPath = "D:\VulkanSDK",
    [string]$QtPath = "D:\Qt\6.8.0\msvc2022_64"
)

Write-Host "=== Neurus Environment Setup ===" -ForegroundColor Cyan

# --- Vulkan SDK ---
if (Test-Path "$VulkanSdkPath\Bin\glslangValidator.exe") {
    Write-Host "[OK] Vulkan SDK found at $VulkanSdkPath" -ForegroundColor Green
    $env:VULKAN_SDK = $VulkanSdkPath
    [Environment]::SetEnvironmentVariable("VULKAN_SDK", $VulkanSdkPath, "User")
    Write-Host "  VULKAN_SDK set to $VulkanSdkPath"
}
else {
    Write-Host "[MISSING] Vulkan SDK not found at $VulkanSdkPath" -ForegroundColor Yellow
    Write-Host "  Download from: https://vulkan.lunarg.com/"
}

# --- Vulkan-HPP Headers ---
$vulkanHppDest = "dep\Vulkan-Hpp\vulkan"
if (Test-Path "$VulkanSdkPath\Include\vulkan\vulkan_raii.hpp") {
    Write-Host "[OK] Copying Vulkan-HPP headers from SDK to $vulkanHppDest" -ForegroundColor Green
    New-Item -ItemType Directory -Force -Path $vulkanHppDest | Out-Null
    Copy-Item -Path "$VulkanSdkPath\Include\vulkan\*" -Destination $vulkanHppDest -Recurse -Force
    Write-Host "  Vulkan-HPP headers copied."
}
else {
    # Fallback: Try cloning from GitHub
    Write-Host "[INFO] Vulkan-HPP not in SDK. Clone manually:" -ForegroundColor Yellow
    Write-Host "  git submodule add https://github.com/KhronosGroup/Vulkan-Hpp.git dep/Vulkan-Hpp"
}

# --- Qt ---
if (Test-Path "$QtPath\bin\qmake.exe") {
    Write-Host "[OK] Qt found at $QtPath" -ForegroundColor Green
    $env:CMAKE_PREFIX_PATH = $QtPath
    [Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", $QtPath, "User")
    Write-Host "  CMAKE_PREFIX_PATH set to $QtPath"
}
else {
    Write-Host "[MISSING] Qt not found at $QtPath" -ForegroundColor Yellow
    Write-Host "  Download from: https://www.qt.io/download"
    Write-Host "  Or install via aqt: pip install aqtinstall && aqt install-qt windows desktop 6.8.0 win64_msvc2022_64"
}

# --- Git Submodules ---
Write-Host ""
Write-Host "=== Git Submodules ===" -ForegroundColor Cyan
$submodules = @(
    @{Path="dep/glm"; Url="https://github.com/g-truc/glm.git"},
    @{Path="dep/googletest"; Url="https://github.com/google/googletest.git"},
    @{Path="dep/stb"; Url="https://github.com/nothings/stb.git"}
)

foreach ($sub in $submodules) {
    if (Test-Path "$($sub.Path)\.git") {
        Write-Host "[OK] Submodule $($sub.Path) already initialized" -ForegroundColor Green
    }
    else {
        Write-Host "[INFO] Adding submodule $($sub.Path)" -ForegroundColor Yellow
        git submodule add $sub.Url $sub.Path 2>&1 | Out-Null
        if ($?) {
            Write-Host "  Submodule $($sub.Path) added." -ForegroundColor Green
        }
        else {
            Write-Host "  Failed to add $($sub.Path). Check network connection." -ForegroundColor Red
        }
    }
}

# --- Build ---
Write-Host ""
Write-Host "=== Build ===" -ForegroundColor Cyan

if ($env:VULKAN_SDK -and $env:CMAKE_PREFIX_PATH) {
    Write-Host "[READY] All prerequisites found. Building..." -ForegroundColor Green
    Write-Host ""

    # Clean previous build
    if (Test-Path "build") { Remove-Item -Recurse -Force "build" }

    # Configure
    cmake --preset default
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] CMake configuration failed." -ForegroundColor Red
        exit 1
    }

    # Build
    cmake --build build/debug --config Debug
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Build failed." -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "=== Build Successful ===" -ForegroundColor Green
    Write-Host "  Run: .\build\debug\Debug\Neurus.exe"
    Write-Host "  Test: cd build\debug; ctest -C Debug --output-on-failure"
}
else {
    Write-Host "[NOT READY] Install the missing prerequisites above, then re-run this script." -ForegroundColor Yellow
}
