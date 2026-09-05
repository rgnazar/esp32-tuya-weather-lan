# Compila e grava o firmware no ESP32.
#
# Usa o arduino-cli embutido na Arduino IDE: o esptool avulso do core falha
# nesta maquina com "Wrong boot mode detected".
#
# Esta placa NAO tem auto-reset confiavel por DTR/RTS. A sequencia que funciona:
#
#   1. segure o BOTAO BOOT;
#   2. com o BOOT ainda apertado, toque no EN/RST e solte o EN;
#   3. rode este script (ou avise quem o vai rodar);
#   4. solte o BOOT quando aparecer "Connecting...".
#
# So um processo por vez abre a porta serial, e a IDE recria o serial-monitor
# sozinha em segundos - por isso ele e' morto aqui, imediatamente antes do
# upload, e nao antes da compilacao.
#
# Uso:  powershell -ExecutionPolicy Bypass -File tools/gravar.ps1 [-Porta COM3] [-SoCompilar]

param(
    [string]$Porta = 'COM3',
    [string]$Fqbn = 'esp32:esp32:esp32',
    [switch]$SoCompilar,
    # Pula a compilacao e grava o binario ja construido. Use na segunda
    # tentativa: entre o toque no EN e o inicio do upload tem que haver o menor
    # tempo possivel - a compilacao no meio da margem para o serial-monitor
    # reabrir a porta e tirar o chip do modo download.
    [switch]$SemCompilar
)

$ErrorActionPreference = 'Stop'

$cli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
if (-not (Test-Path $cli)) { throw "arduino-cli da IDE nao encontrado em: $cli" }

$sketch = Join-Path (Split-Path $PSScriptRoot -Parent) 'estacaolocal'
if (-not (Test-Path (Join-Path $sketch 'estacaolocal.ino'))) { throw "Sketch nao encontrado em: $sketch" }

if ($SemCompilar) {
    Write-Output 'Pulando a compilacao (-SemCompilar).'
} else {
    Write-Output "Compilando $sketch ..."
    & $cli compile --fqbn $Fqbn $sketch
    if ($LASTEXITCODE -ne 0) { throw "Compilacao falhou (exit $LASTEXITCODE)." }
}

if ($SoCompilar) { Write-Output 'Compilado. Nada gravado (-SoCompilar).'; return }

# Libera a porta serial no ultimo instante.
Get-Process serial-monitor -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process putty, plink -ErrorAction SilentlyContinue | Stop-Process -Force

Write-Output "`nGravando em $Porta - solte o BOOT quando aparecer 'Connecting...'`n"
& $cli upload --fqbn $Fqbn --port $Porta $sketch
if ($LASTEXITCODE -ne 0) {
    throw "Upload falhou (exit $LASTEXITCODE). 'Wrong boot mode detected' = refazer a sequencia do BOOT/EN."
}
Write-Output "`nGravado."
