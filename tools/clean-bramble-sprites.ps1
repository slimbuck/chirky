$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$game = Join-Path $root "games/bramble-hollow"
$work = Join-Path $root "build/bramble-sprite-clean"
$playerSource = Join-Path $game "assets/artwork/sources/player-sheet-polished.png"
$friendSource = Join-Path $game "assets/artwork/sources/friends-sheet-polished.png"

if (-not (Get-Command magick -ErrorAction SilentlyContinue)) {
    throw "ImageMagick's magick command is required."
}

function Get-ImageSize($path) {
    $parts = (& magick identify -format "%w %h" $path) -split " "
    if ($LASTEXITCODE -or $parts.Count -ne 2) { throw "Unable to inspect $path." }
    return @([int]$parts[0], [int]$parts[1])
}

function Get-GridCrop($width, $height, $columns, $rows, $column, $row) {
    $left = [math]::Floor($column * $width / $columns)
    $right = [math]::Floor(($column + 1) * $width / $columns)
    $top = [math]::Floor($row * $height / $rows)
    $bottom = [math]::Floor(($row + 1) * $height / $rows)
    return "$($right - $left)x$($bottom - $top)+$left+$top"
}

New-Item -ItemType Directory -Force $work | Out-Null
$playerSize = Get-ImageSize $playerSource
$playerRows = @()
for ($row = 0; $row -lt 4; $row++) {
    $cells = @()
    for ($column = 0; $column -lt 4; $column++) {
        $cell = Join-Path $work "player-$column-$row.png"
        $crop = Get-GridCrop $playerSize[0] $playerSize[1] 4 4 $column $row
        & magick $playerSource -crop $crop +repage `
            -channel A -threshold "55%" +channel -trim +repage -filter point `
            -resize "28x34" +dither -colors 24 `
            -gravity south -background none -extent "38x38" `
            -gravity center -extent "40x40" $cell
        if ($LASTEXITCODE) { throw "Unable to prepare player frame $column,$row." }
        $cells += $cell
    }
    $packed = Join-Path $work "player-row-$row.png"
    & magick @cells +append $packed
    if ($LASTEXITCODE) { throw "Unable to pack player row $row." }
    $playerRows += $packed
}
& magick @playerRows -append "PAM:$(Join-Path $game 'assets/player.pam')"
if ($LASTEXITCODE) { throw "Unable to write the player atlas." }

$friendCrops = @(
    "291x446+96+54", "322x417+500+83", "321x459+952+41", "351x404+1362+98",
    "312x325+73+535", "317x325+486+536", "456x286+865+575", "243x202+1446+639"
)
$friendTargets = @("26x36", "28x36", "30x40", "28x36", "26x36", "26x36", "46x28!", "22x24")
$friendRows = @()
for ($row = 0; $row -lt 2; $row++) {
    $cells = @()
    for ($column = 0; $column -lt 4; $column++) {
        $index = $row * 4 + $column
        $cell = Join-Path $work "friend-$column-$row.png"
        $crop = $friendCrops[$index]
        & magick $friendSource -crop $crop +repage `
            -channel A -threshold "55%" +channel -trim +repage -filter point `
            -resize $friendTargets[$index] +dither -colors 24 `
            -gravity south -background none -extent "48x43" `
            -gravity center -extent "50x45" $cell
        if ($LASTEXITCODE) { throw "Unable to prepare friend frame $column,$row." }
        if ($index -eq 2) {
            & magick $cell -fill "#6e4529" -draw `
                "rectangle 11,21 15,21 rectangle 10,23 15,23 rectangle 11,25 15,25 rectangle 34,21 38,21 rectangle 34,23 39,23 rectangle 34,25 38,25" $cell
            if ($LASTEXITCODE) { throw "Unable to preserve the cat whiskers." }
        }
        $cells += $cell
    }
    $packed = Join-Path $work "friend-row-$row.png"
    & magick @cells +append $packed
    if ($LASTEXITCODE) { throw "Unable to pack friend row $row." }
    $friendRows += $packed
}
& magick @friendRows -append "PAM:$(Join-Path $game 'assets/friends.pam')"
if ($LASTEXITCODE) { throw "Unable to write the friend atlas." }

Write-Output "Built polished Bramble Hollow runtime atlases."
