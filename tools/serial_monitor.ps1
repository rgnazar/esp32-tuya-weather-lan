# Le o monitor serial do ESP32 por um tempo determinado.
#
# Uso:  powershell -ExecutionPolicy Bypass -File tools/serial_monitor.ps1 -Port COM3 -Seconds 90
#
# Por padrao pulsa o RTS antes de ler, o que reinicia a placa e permite
# acompanhar desde o boot. Use -NoReset para apenas escutar a execucao em curso.

param(
    [string]$Port = 'COM3',
    [int]$Baud = 115200,
    [int]$Seconds = 90,
    [switch]$NoReset
)

$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
# DTR baixo mantem o GPIO0 alto: garante boot normal, nao modo de gravacao.
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.ReadTimeout = 500

$sp.Open()
try {
    if (-not $NoReset) {
        # Pulso no RTS -> pino EN -> reset da placa.
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 120
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 300
        $sp.DiscardInBuffer()
    }

    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $sp.ReadLine()
            Write-Output $line.TrimEnd()
        } catch [TimeoutException] {
            # sem dados nesta janela; segue esperando
        }
    }
} finally {
    $sp.Close()
}
