# Identifica dispositivos Tuya na LAN decifrando os broadcasts UDP 6667.
#
# Os dispositivos anunciam-se a cada poucos segundos com um payload cifrado em
# AES-128-ECB, usando uma chave publica conhecida (a mesma em todo o ecossistema
# Tuya). Decifrando, obtemos o gwId - o identificador que casa com o
# TUYA_DEVICE_ID do config.h e revela qual IP e' a estacao.
#
# Usa o AES do proprio Windows: nao requer instalar nada.
#
# Uso:  powershell -ExecutionPolicy Bypass -File tools/tuya_lan_identify.ps1 [-Seconds 30]

param(
    [int]$Seconds = 30,
    [int]$Port = 6667
)

$ErrorActionPreference = 'Stop'

# Device procurado, lido de tools/config.py. O firmware nao guarda o device ID:
# o protocolo local se autentica so pela LOCAL_KEY. Quem precisa dele e' este
# script, para saber qual dos dispositivos Tuya da rede e' a estacao.
$configPath = Join-Path $PSScriptRoot 'config.py'
$wantedId = $null
if (Test-Path $configPath) {
    $m = Select-String -Path $configPath -Pattern '^\s*DEVICE_ID\s*=\s*"([^"]+)"'
    if ($m) { $wantedId = $m.Matches[0].Groups[1].Value }
}
Write-Output "Device procurado: $(if ($wantedId) { $wantedId } else { '(defina DEVICE_ID em tools/config.py)' })"

$key = [System.Security.Cryptography.MD5]::Create().ComputeHash(
    [Text.Encoding]::ASCII.GetBytes('yGAdlopoPVldABfn'))

$aes = [System.Security.Cryptography.Aes]::Create()
$aes.Mode = [System.Security.Cryptography.CipherMode]::ECB
$aes.Padding = [System.Security.Cryptography.PaddingMode]::None
$aes.Key = $key
$decryptor = $aes.CreateDecryptor()

$udp = New-Object System.Net.Sockets.UdpClient
$udp.Client.SetSocketOption('Socket', 'ReuseAddress', $true)
$udp.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, $Port)))
$udp.Client.ReceiveTimeout = 1000

$endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
$seen = @{}
$deadline = (Get-Date).AddSeconds($Seconds)

Write-Output "Escutando UDP $Port por ${Seconds}s...`n"

while ((Get-Date) -lt $deadline) {
    try { $data = $udp.Receive([ref]$endpoint) } catch { continue }

    $ip = $endpoint.Address.ToString()
    if ($seen.ContainsKey($ip)) { continue }

    # Estrutura do pacote: cabecalho de 20 bytes, payload, depois CRC + sufixo.
    if ($data.Length -le 28) { continue }
    $len = $data.Length - 20 - 8
    $len = $len - ($len % 16)          # AES-ECB exige multiplo do bloco
    if ($len -le 0) { continue }

    $payload = New-Object byte[] $len
    [Array]::Copy($data, 20, $payload, 0, $len)

    try {
        $plain = $decryptor.TransformFinalBlock($payload, 0, $payload.Length)
    } catch { continue }

    # Remove o preenchimento PKCS7.
    $pad = $plain[$plain.Length - 1]
    if ($pad -gt 0 -and $pad -le 16) { $plain = $plain[0..($plain.Length - $pad - 1)] }

    $text = [Text.Encoding]::UTF8.GetString($plain)
    try { $info = $text | ConvertFrom-Json } catch { continue }

    $seen[$ip] = $info
    $gw = $info.gwId
    $mark = if ($wantedId -and $gw -eq $wantedId) { '   <=== A ESTACAO' } else { '' }
    Write-Output ("{0,-15} gwId={1} versao={2}{3}" -f $ip, $gw, $info.version, $mark)
}

$udp.Close()

Write-Output "`n=== conclusao ==="
$match = $seen.GetEnumerator() | Where-Object { $_.Value.gwId -eq $wantedId }
if ($match) {
    $ip = $match.Key
    Write-Output "A estacao esta em $ip (protocolo versao $($match.Value.version))."
    $tcp = New-Object System.Net.Sockets.TcpClient
    $open = $false
    try {
        $open = $tcp.ConnectAsync($ip, 6668).Wait(2000) -and $tcp.Connected
    } catch { }
    finally { $tcp.Close() }
    Write-Output "Porta 6668: $(if ($open) { 'ABERTA - protocolo local viavel' } else { 'FECHADA' })"
} else {
    Write-Output "Nenhum broadcast casou com o TUYA_DEVICE_ID."
    Write-Output "Dispositivos vistos: $($seen.Count)"
}
