[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sourcePath = [IO.Path]::GetFullPath($Source)
$destinationPath = [IO.Path]::GetFullPath($Destination)
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw 'The chosen source-approved app icon is missing.' }
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
$image = [Drawing.Image]::FromFile($sourcePath)
try {
    foreach ($asset in @(@('Square44x44Logo.png', 44), @('Square150x150Logo.png', 150), @('StoreLogo.png', 50))) {
        $size = [int]$asset[1]
        $bitmap = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
            $scale = [Math]::Min($size / $image.Width, $size / $image.Height)
            $width = [int][Math]::Round($image.Width * $scale)
            $height = [int][Math]::Round($image.Height * $scale)
            $x = [int][Math]::Floor(($size - $width) / 2)
            $y = [int][Math]::Floor(($size - $height) / 2)
            $graphics.DrawImage($image, [Drawing.Rectangle]::new($x, $y, $width, $height))
            $bitmap.Save((Join-Path $destinationPath $asset[0]), [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally { $image.Dispose() }
# Only installer logos are derived. Every staged game/runtime texture remains byte-exact.
