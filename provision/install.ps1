<#
.SYNOPSIS
  Provision and install Two Forty on a freshly booted Raspberry Pi OS Trixie Pi.

.EXAMPLE
  .\provision\install.ps1 -HostName twoforty.local
  .\provision\install.ps1 -HostName 192.168.137.2 -IdentityFile $env:USERPROFILE\.ssh\twoforty
#>
[CmdletBinding()]
param(
  [string]$HostName = "192.168.137.2",
  [string]$PiName = "twoforty",
  [string]$User = "retro",
  [string]$Address = "192.168.137.2/24",
  [string]$BootGame = "launcher",
  [string]$IdentityFile,
  [string]$KnownHostsFile,
  [switch]$Offline,
  [switch]$SkipNetwork,
  [switch]$NoReboot
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$remoteRoot = "/home/$User/two-forty"
$target = "$User@$HostName"
$sshOptions = @("-o", "BatchMode=yes", "-o", "ConnectTimeout=10")
if ($IdentityFile) { $sshOptions += @("-i", $IdentityFile) }
if ($KnownHostsFile) {
  $sshOptions += @("-o", "UserKnownHostsFile=$KnownHostsFile", "-o", "StrictHostKeyChecking=yes")
} else {
  $sshOptions += @("-o", "StrictHostKeyChecking=accept-new")
}

Write-Host "==> Creating $remoteRoot on $target" -ForegroundColor Cyan
& ssh @sshOptions $target "sudo install -d -o '$User' -g '$User' '$remoteRoot'"
if ($LASTEXITCODE -ne 0) { throw "Could not prepare the remote project directory." }

$sources = @("Makefile", "README.md", "include", "src", "assets", "games", "tools", "deploy", "provision", "config") |
  ForEach-Object { Join-Path $projectRoot $_ }
Write-Host "==> Copying reproducible source and assets" -ForegroundColor Cyan
& scp @sshOptions -r @sources "${target}:$remoteRoot/"
if ($LASTEXITCODE -ne 0) { throw "Source deployment failed." }

$networkOption = if ($SkipNetwork) { " --skip-network" } else { "" }
$offlineOption = if ($Offline) { " --offline" } else { "" }
$remoteCommand = "cd '$remoteRoot' && sudo sh provision/provision-pi.sh --user '$User' --hostname '$PiName' --address '$Address' --boot-game '$BootGame'$networkOption && sudo sh provision/install-two-forty.sh --user '$User'$offlineOption"
Write-Host "==> Applying Trixie/RGBerry configuration and building Two Forty" -ForegroundColor Cyan
& ssh @sshOptions $target $remoteCommand
if ($LASTEXITCODE -ne 0) { throw "Remote provisioning or installation failed." }

if ($NoReboot) {
  Write-Host "Installed successfully. Reboot the Pi before testing SCART output." -ForegroundColor Yellow
} else {
  Write-Host "==> Rebooting; it will return at 192.168.137.2 and boot $BootGame" -ForegroundColor Cyan
  & ssh @sshOptions $target "sudo systemctl reboot" | Out-Null
}
