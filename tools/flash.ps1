<#
.SYNOPSIS
    Graba el firmware en el Cardputer por USB.

.PARAMETER Port
    Puerto COM del Cardputer (Administrador de dispositivos). Por defecto COM4.

.PARAMETER Bin
    Imagen completa a grabar en 0x0. Por defecto firmware\Bruce-m5stack-cardputer.bin

.PARAMETER Esptool
    Ruta a esptool.exe. Por defecto el que instala PlatformIO en el perfil del usuario.
    Util si la consola corre con otro usuario (por ejemplo, como administrador).

.EXAMPLE
    .\tools\flash.ps1 -Port COM4
#>
param(
    [string]$Port = 'COM4',
    [string]$Bin = (Join-Path (Split-Path -Parent $PSScriptRoot) 'firmware\Bruce-m5stack-cardputer.bin'),
    [string]$Esptool = (Join-Path $env:USERPROFILE '.platformio\penv\Scripts\esptool.exe')
)
$ErrorActionPreference = 'Stop'

$esptoolArgs = @('--chip', 'esp32s3', '--port', $Port, '--baud', '921600', 'write-flash', '0x0', $Bin)

Write-Host "Grabando $Bin en $Port ..."
# esptool: primero el de PlatformIO; si no existe, el modulo de Python (pip install esptool)
if (Test-Path $Esptool) { & $Esptool @esptoolArgs } else { & python -m esptool @esptoolArgs }
if ($LASTEXITCODE -ne 0) {
    Write-Host 'Fallo. Proba poner el Cardputer en modo descarga: mantener G0 apretado al conectar el USB.' -ForegroundColor Yellow
    exit 1
}
Write-Host 'Listo. Desconecta y reconecta el Cardputer (o reinicialo).' -ForegroundColor Green
