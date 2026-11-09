<#
.SYNOPSIS
  Builds and runs the tests of SKIV's animated image decoders (src\utility\animated_images.inl), ported from SKIF.

.DESCRIPTION
  Builds image_tests.cpp with the decoders, stb_image and libwebp of the repository (packages_misc), and runs it on
  the test images (images\). It checks, for APNG, GIF and WebP:

    * The frames, their size and delays, against what reference decoders make of them (expected.txt, frames\:
      ffmpeg for APNG and GIF, Pillow for WebP), including blending and each way of disposing of a frame.
    * That decoding stops when it is told to.
    * IsAnimatedImageFile, for animated and still images, files that are not images, and a missing file.
    * Still WebPs (as the viewer loads them).
    * Color sources (color_source.inl) and conversions (color_convert.inl, with Little CMS): ICC profiles, cICP,
      PNG chunks and Exif, which of them are sRGB, linear or HDR10, and what wide gamut colors convert to.
    * Malformed files: cut off at many points, or not images at all. They must fail (or keep the frames before
      the cut) and never crash. With -Sanitize, reading or writing outside a buffer is caught as well.

  On Windows it uses the C++ compiler of Visual Studio (found with vswhere); elsewhere g++ and gcc (or $env:CXX
  and $env:CC). The build goes to bin\ (ignored by git). Returns 0 if all the tests pass.
  The test images, and what they should decode to, are made by make_images.py and make_expected.py; they only
  need to be made again when the images change.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\image_tests\run_tests.ps1

.EXAMPLE
  pwsh -File tools/image_tests/run_tests.ps1 -Sanitize
#>

[CmdletBinding()]
param (
  # Build with AddressSanitizer (and UndefinedBehaviorSanitizer, with gcc), to catch reads and writes outside a buffer
  [switch] $Sanitize
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$here   = $PSScriptRoot
$root   = (Resolve-Path (Join-Path $here '..\..')).Path
$bin    = Join-Path $here 'bin'
$misc   = Join-Path $root 'packages_misc'
$webp   = Join-Path $misc 'libwebp'
$lcms   = Join-Path $misc 'lcms2'
$source = Join-Path $here 'image_tests.cpp'

# The C libraries: libwebp, and Little CMS (for color_convert.inl)
$webpSources = @(Get-ChildItem -Path (Join-Path $webp 'src') -Filter '*.c' -Recurse | ForEach-Object { $_.FullName })
$lcmsSources = @(Get-ChildItem -Path (Join-Path $lcms 'src') -Filter '*.c' | ForEach-Object { $_.FullName })

if ($webpSources.Count -eq 0) {
  throw "libwebp was not found in $webp"
}

if ($lcmsSources.Count -eq 0) {
  throw "Little CMS was not found in $lcms"
}

$webpSources += $lcmsSources

New-Item -ItemType Directory -Force -Path $bin | Out-Null

$onWindows = ($PSVersionTable.PSVersion.Major -lt 6) -or $IsWindows

if ($onWindows) {
  # The Visual Studio installation with the C++ compiler, and its environment (vcvars64.bat)
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

  if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio was not found (no vswhere.exe)'
  }

  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

  if (-not $vs) {
    throw 'Visual Studio with the C++ compiler was not found'
  }

  $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
  $exe    = Join-Path $bin 'image_tests.exe'
  $common = "/nologo /MP /O2 /Zi /utf-8 /I`"$misc`" /I`"$webp`" /I`"$lcms\include`" /Fo`"$bin\\`" /Fd`"$bin\\`""

  if ($Sanitize) {
    $common += ' /fsanitize=address'
  }

  # libwebp and Little CMS as C (warnings off, as in SKIV.vcxproj), then the tests as C++. The lists of their files go
  #   into response files (one file a line): on the command line, they would exceed the 8191 characters cmd.exe allows
  #     once the repository is in a long enough path.
  $cRsp   = Join-Path $bin 'sources.rsp'
  $objRsp = Join-Path $bin 'objects.rsp'
  $script = Join-Path $bin 'build.cmd'

  $webpSources | ForEach-Object { "`"$_`"" } | Set-Content -Path $cRsp -Encoding ASCII
  $webpSources | ForEach-Object { "`"$(Join-Path $bin ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj'))`"" } |
    Set-Content -Path $objRsp -Encoding ASCII

  @(
    '@echo off'
    "call `"$vcvars`" >nul || exit /b 1"
    "cl $common /W0 /c `"@$cRsp`" || exit /b 1"
    "cl $common /W3 /std:c++17 /EHsc `"$source`" `"@$objRsp`" /Fe`"$exe`" /link /DEBUG || exit /b 1"
  ) | Set-Content -Path $script -Encoding ASCII

  & cmd.exe /c "`"$script`""
}

else {
  $cxx   = if ($env:CXX) { $env:CXX } else { 'g++' }
  $cc    = if ($env:CC)  { $env:CC  } else { 'gcc' }
  $exe   = Join-Path $bin 'image_tests'
  $flags = @('-O1', '-g', "-I$misc", "-I$webp", "-I$lcms/include")

  if ($Sanitize) {
    $flags += '-fsanitize=address,undefined', '-fno-omit-frame-pointer'
  }

  $objs = @()

  foreach ($file in $webpSources) {
    $obj   = Join-Path $bin ([IO.Path]::GetFileNameWithoutExtension($file) + '.o')
    $objs += $obj

    & $cc @flags -w -c $file -o $obj

    if ($LASTEXITCODE -ne 0) {
      throw "Could not build $file"
    }
  }

  & $cxx @flags -std=c++17 -Wall $source @objs -o $exe -lpthread
}

if ($LASTEXITCODE -ne 0) {
  throw 'The tests could not be built'
}

& $exe $here
exit $LASTEXITCODE
