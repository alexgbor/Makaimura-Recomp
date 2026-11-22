<#
    Name:        make_release.ps1
    Description: the player's package of the game: release\Makaimura-<version>-win64.zip, for a GitHub Release
    Version:     0.1.0
    Created:     20-11-2026

    license:BSD-3-Clause
    After DonPachi Recomp's tools/make_release.ps1.

    Run it from the project's folder after a Release build:
        cmake --build build/msvc --config Release
        powershell -ExecutionPolicy Bypass -File tools\make_release.ps1

    The version is CMakeLists.txt's (project(makaimura VERSION x.y.z)), the one the game's menu shows: change it
    there, build, then package. -Version, if given, must be the same.

    It copies only the files named below (an allow list: nothing else can get in), then checks the folder: no ROM
    set, no settings, high scores, states or replays of yours, and an .exe that needs nothing but SDL2.dll beside it.
    Any problem stops it before the .zip is made. The player brings the ROM set (MAME's makaimurg.zip and gng.zip),
    which the game asks for on its first run.
#>
param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# the version: CMakeLists.txt's, so the package and the game's menu agree
$cmake = Get-Content "CMakeLists.txt" -Raw
if ($cmake -notmatch 'project\(makaimura\s+VERSION\s+(\d+\.\d+\.\d+)') { throw "No 'project(makaimura VERSION x.y.z' in CMakeLists.txt" }
$built = $Matches[1]
if ($Version -eq "") { $Version = $built }
elseif ($Version -ne $built) {
    throw "-Version $Version, but CMakeLists.txt says $built (the game would show $built): change it there and build again"
}

$name = "Makaimura-$Version-win64"
$out = Join-Path $root "release\$name"
$zip = Join-Path $root "release\$name.zip"

# ---- what goes in: the only files the package can hold (from -> to) ----
$sdlLicense = Get-ChildItem "build\downloads\sdl2\SDL2-*\COPYING.txt" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $sdlLicense) { throw "No SDL2 COPYING.txt in build\downloads\sdl2 (the SDL2 development package unpacked there)" }
$files = [ordered]@{
    "build\bin\makaimura.exe"          = "makaimura.exe"
    "build\bin\SDL2.dll"               = "SDL2.dll"
    "ports\sdl2\release\README.txt"    = "README.txt"
    "CHANGELOG.md"                     = "CHANGES.txt"
    "LICENSE"                          = "licenses\LICENSE.txt"
    "ports\sdl2\release\NOTICES.txt"   = "licenses\NOTICES.txt"
}
$files[$sdlLicense.FullName.Substring($root.Length + 1)] = "licenses\SDL2.txt"

foreach ($from in $files.Keys) {
    if (-not (Test-Path $from -PathType Leaf)) { throw "Missing: $from (build the Release first?)" }
}

# the changelog has this version's section, and nothing waiting under Unreleased
$changes = Get-Content "CHANGELOG.md" -Raw
if ($changes -notmatch "(?m)^## v$([regex]::Escape($Version))\b") {
    throw "CHANGELOG.md has no '## v$Version' section: rename its '## Unreleased' to '## v$Version (date)' first"
}
if ($changes -match "(?ms)^## Unreleased\s*(.*?)^## v") {
    $waiting = $Matches[1].Trim()
    if ($waiting -ne "" -and $waiting -ne "Nothing yet.") {
        throw "CHANGELOG.md lists changes under '## Unreleased' that v$Version doesn't have: make them a new version first"
    }
}

# the .exe newer than the sources: a build made after the last change
$exe = Get-Item "build\bin\makaimura.exe"
$newest = Get-ChildItem "ports\sdl2", "runtime", "src" -Recurse -Include *.c, *.h | Sort-Object LastWriteTime | Select-Object -Last 1
if ($newest.LastWriteTime -gt $exe.LastWriteTime) {
    throw "build\bin\makaimura.exe is older than $($newest.FullName): build the Release again first"
}

# ---- the folder, made fresh ----
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
New-Item -ItemType Directory -Path (Join-Path $out "licenses") -Force | Out-Null
foreach ($from in $files.Keys) {
    Copy-Item $from (Join-Path $out $files[$from])
}
# the text files with Windows' line ends (for Notepad)
foreach ($text in "README.txt", "CHANGES.txt", "licenses\LICENSE.txt", "licenses\NOTICES.txt") {
    $path = Join-Path $out $text
    $content = (Get-Content $path -Raw).Replace("`r`n", "`n").Replace("`n", "`r`n")
    Set-Content $path $content -NoNewline -Encoding ASCII
}

# ---- the checks ----
$problems = @()
$allowed = @($files.Values)
foreach ($file in Get-ChildItem $out -Recurse -File) {
    $relative = $file.FullName.Substring($out.Length + 1)
    if ($allowed -notcontains $relative) { $problems += "not on the list: $relative" }
    if ($file.Extension -in ".zip", ".7z", ".dat", ".part", ".hi", ".state", ".inp", ".ini", ".bin", ".rom", ".regions", ".avi") {
        $problems += "a ROM set, game data, save or settings file: $relative"
    }
}
# the ROM set's own files (MAME's names), in case one were renamed into the list
foreach ($romName in "mj03g.bin", "mj04g.bin", "mj05g.bin", "gg1.bin", "gg2.bin", "makaimurg.regions") {
    if (Get-ChildItem $out -Recurse -Filter $romName) { $problems += "a ROM file: $romName" }
}
# the .exe: 64-bit, not too large, needing no Visual C++ runtime, and none of the ROM's graphics or sound in it
$bytes = [System.IO.File]::ReadAllBytes((Join-Path $out "makaimura.exe"))
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
if ([BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) { $problems += "makaimura.exe is not a 64-bit build" }
if ($bytes.Length -gt 16MB) { $problems += "makaimura.exe is $([int]($bytes.Length / 1MB)) MB: more than the program should be" }
# (the game is the readable C, src/: a message only its runtime has, runtime/src/m6809_program.c; one only the
# translation's has, runtime/src/m6809.c, not there)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
if (-not $text.Contains("the main loop returned") -or $text.Contains("an address the static analysis did not find")) {
    $problems += "makaimura.exe is not the readable C (python tools/m6809/decompile.py, then build again)"
}
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
if ($text -match "(?i)vcruntime\d+d?\.dll|msvcp\d+d?\.dll|ucrtbased\.dll") {
    $problems += "makaimura.exe needs the Visual C++ runtime ($($Matches[0])): build with the static runtime"
}
# (a stretch of the ROM set's graphics or sound program in the .exe would mean its data had got in: the first
# 256 bytes of each file, looked for)
foreach ($romZip in "rom\makaimurg.zip", "rom\gng.zip") {
    if (-not (Test-Path $romZip)) { continue }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead((Join-Path $root $romZip))
    foreach ($entry in $archive.Entries) {
        if ($entry.Length -lt 4096) { continue }
        $stream = $entry.Open(); $head = New-Object byte[] 256; [void]$stream.Read($head, 0, 256); $stream.Close()
        $needle = [System.Text.Encoding]::GetEncoding(28591).GetString($head)
        $hay = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
        if ($hay.Contains($needle)) { $problems += "makaimura.exe holds the start of $($entry.Name) from $romZip" }
    }
    $archive.Dispose()
}

if ($problems.Count -gt 0) {
    Remove-Item $out -Recurse -Force
    Write-Host "No package made:" -ForegroundColor Red
    $problems | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

# (each entry named with "/" by hand: Compress-Archive and .NET Framework's ZipFile both write the folders with "\",
# which other systems' unzip tools take as part of the file names)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
foreach ($file in Get-ChildItem $out -Recurse -File) {
    $entry = "$name/" + $file.FullName.Substring($out.Length + 1).Replace("\", "/")
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry,
        [System.IO.Compression.CompressionLevel]::Optimal)
}
$archive.Dispose()
Write-Host "Made $zip" -ForegroundColor Green
Get-ChildItem $out -Recurse -File | ForEach-Object {
    "  {0,-28} {1,12:N0} bytes" -f $_.FullName.Substring($out.Length + 1), $_.Length
}
