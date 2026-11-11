<#
.SYNOPSIS
    Arma el firmware Bruce + Serial Monitor para el M5Stack Cardputer.

.DESCRIPTION
    1. Descarga Bruce en la version probada (si no esta descargado).
    2. Copia la app (src/) dentro del arbol de Bruce y aplica patches/bruce-integration.patch.
    3. Compila con PlatformIO validando HTTPS con el almacen de certificados de Windows
       (necesario detras de un proxy corporativo que re-firma HTTPS).
    4. Copia el binario resultante a firmware/.

.PARAMETER BruceDir
    Carpeta de trabajo de Bruce (fuera de Documentos para evitar "Acceso controlado a carpetas"
    de Windows Defender). Por defecto: %USERPROFILE%\src\Bruce

.EXAMPLE
    .\tools\build.ps1
.EXAMPLE
    .\tools\build.ps1 -BruceDir D:\work\Bruce
#>
param(
    [string]$BruceDir = (Join-Path $env:USERPROFILE 'src\Bruce'),
    [string]$BruceRepo = 'https://github.com/pr3y/Bruce.git',
    [string]$BruceCommit = 'a59213f3ec6cd302fa32c2042b2700c9c24d991a',
    [string]$PioEnv = 'm5stack-cardputer'
)
$ErrorActionPreference = 'Stop'
$RepoDir = Split-Path -Parent $PSScriptRoot

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }

# --- 1. Bruce ---------------------------------------------------------------
if (-not (Test-Path (Join-Path $BruceDir '.git'))) {
    Step "Descargando Bruce $BruceCommit en $BruceDir"
    New-Item -ItemType Directory -Force $BruceDir | Out-Null
    git -C $BruceDir init -q
    git -C $BruceDir remote add origin $BruceRepo
    git -C $BruceDir fetch --depth 1 origin $BruceCommit
    git -C $BruceDir checkout -q FETCH_HEAD
} else {
    Step "Usando Bruce existente en $BruceDir"
}

# --- 2. App + parche --------------------------------------------------------
Step 'Copiando la app al arbol de Bruce'
Copy-Item -Recurse -Force (Join-Path $RepoDir 'src\*') (Join-Path $BruceDir 'src')

$patch = Join-Path $RepoDir 'patches\bruce-integration.patch'
git -C $BruceDir apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    Step 'El parche de integracion ya estaba aplicado'
} else {
    Step 'Aplicando el parche de integracion'
    git -C $BruceDir apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo aplicar el parche (version de Bruce distinta?)' }
}

# --- 3. Requisitos de Python -------------------------------------------------
Step 'Verificando PlatformIO y truststore'
python -c "import platformio, truststore" 2>$null
if ($LASTEXITCODE -ne 0) {
    python -m pip install --user platformio truststore
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo instalar platformio/truststore' }
}

# Arreglo: si una compilacion anterior se corto, patch.py de Bruce puede dejar
# libnet80211.a renombrada a .old sin la version parcheada (error "cannot find -lnet80211").
$libDir = Join-Path $env:USERPROFILE '.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib'
$objcopy = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-objcopy.exe'
if ((Test-Path "$libDir\libnet80211.a.old") -and -not (Test-Path "$libDir\libnet80211.a") -and (Test-Path $objcopy)) {
    Step 'Regenerando libnet80211.a parcheada'
    & $objcopy --weaken-symbol=ieee80211_raw_frame_sanity_check "$libDir\libnet80211.a.old" "$libDir\libnet80211.a"
}

# --- 4. Compilar --------------------------------------------------------------
Step "Compilando entorno $PioEnv (la primera vez descarga ~2 GB de herramientas)"
$env:PYTHONPATH = Join-Path $PSScriptRoot 'pio-winstore-site'
python (Join-Path $PSScriptRoot 'pio_winstore.py') run -e $PioEnv -d $BruceDir
if ($LASTEXITCODE -ne 0) { throw "La compilacion fallo (ver doc/DESARROLLO.md, seccion Problemas)" }

$bin = Join-Path $BruceDir "Bruce-$PioEnv.bin"
Copy-Item -Force $bin (Join-Path $RepoDir 'firmware\')
Step "Listo: firmware\Bruce-$PioEnv.bin"
