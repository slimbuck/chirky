# Export the editable native-size PNGs for the game runtime. Requires ImageMagick.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
foreach ($gameId in @('phosphor-run', 'rosey-chop')) {
    $artDirectory = Join-Path $projectRoot "games/$gameId/assets/artwork"
    $pngPath = Join-Path $artDirectory 'splash.png'
    $dimensions = & magick identify -format '%wx%h' $pngPath
    if ($LASTEXITCODE -ne 0 -or $dimensions -ne '288x216') { throw "$gameId artwork must be 288x216" }
    & magick $pngPath -background black -alpha remove -alpha off -colorspace sRGB -type TrueColor -depth 8 (Join-Path $artDirectory 'splash.ppm')
    if ($LASTEXITCODE -ne 0) { throw "Splash export failed: $gameId" }
}

# Historical PCB artwork only. The current mascot and font are built by
# python tools/build-launcher-art.py; this export is not loaded by the launcher.
$launcherDirectory = Join-Path $projectRoot "assets/launcher"
$launcherPng = Join-Path $launcherDirectory "splash.png"
$launcherSize = & magick identify -format "%wx%h" $launcherPng
if ($LASTEXITCODE -ne 0 -or $launcherSize -ne "320x240") { throw "Launcher artwork must be 320x240" }
& magick $launcherPng -background black -alpha remove -alpha off -colorspace sRGB -type TrueColor -depth 8 (Join-Path $launcherDirectory "splash.ppm")
if ($LASTEXITCODE -ne 0) { throw "Launcher export failed" }
