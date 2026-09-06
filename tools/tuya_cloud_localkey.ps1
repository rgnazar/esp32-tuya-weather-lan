# Busca a local_key do dispositivo na Tuya Cloud.
#
#   GET /v1.0/devices/{device_id}   ->   campo "local_key"
#
# A local_key nao aparece na interface do console de IoT: so pela API. Este
# script faz o mesmo que o API Explorer, mas da linha de comando - util para
# reobter a chave depois de reparear a estacao no aplicativo, quando o firmware
# passa a registrar "HMAC do dispositivo nao confere".
#
# Sao necessarios o Access ID e o Access Secret do projeto de nuvem
# (Cloud -> Development -> seu projeto -> Overview -> Authorization Key) e o
# data center em que o projeto foi criado.
#
# Nao requer instalar nada: usa o HMAC-SHA256 do proprio Windows.
#
# Readicionar a estacao no aplicativo troca tambem o device ID, e ai' o antigo
# passa a responder 1106. Nesse caso comece pelo -List, que mostra os
# dispositivos do projeto com id, categoria e local_key.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools/tuya_cloud_localkey.ps1
#   powershell -ExecutionPolicy Bypass -File tools/tuya_cloud_localkey.ps1 -List
#   powershell -ExecutionPolicy Bypass -File tools/tuya_cloud_localkey.ps1 `
#       -AccessId xxxx -DeviceId eb0000... -Region us
#
# Sem parametros, le TUYA_ACCESS_ID, TUYA_ACCESS_SECRET, TUYA_REGION e
# DEVICE_ID de tools/config.py (que esta no .gitignore). O secret tambem pode
# ser digitado no prompt, sem eco - assim nao fica no historico do shell.

param(
    [string]$AccessId,
    [string]$AccessSecret,
    [string]$DeviceId,
    # Data center do projeto de nuvem. Para o Brasil costuma ser 'us'
    # (Western America) - o mesmo escolhido ao criar o projeto. Aceita tanto o
    # codigo curto quanto o nome exibido no console ('Western America Data
    # Center'), que e' o que se costuma copiar de la.
    [string]$Region,
    # Lista todos os dispositivos do projeto em vez de consultar um so'. Util
    # quando a estacao foi readicionada no aplicativo: o device ID muda, e o
    # antigo passa a devolver erro 1106.
    [switch]$List
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$endpoints = @{
    'us'   = 'https://openapi.tuyaus.com'       # Western America
    'us-e' = 'https://openapi-ueaz.tuyaus.com'  # Eastern America
    'eu'   = 'https://openapi.tuyaeu.com'       # Central Europe
    'eu-w' = 'https://openapi-weaz.tuyaeu.com'  # Western Europe
    'cn'   = 'https://openapi.tuyacn.com'       # China
    'in'   = 'https://openapi.tuyain.com'       # India
}

# ---------------------------------------------------------------- config.py

$configPath = Join-Path $PSScriptRoot 'config.py'

function Get-ConfigValue([string]$Name) {
    if (-not (Test-Path $configPath)) { return $null }
    $m = Select-String -Path $configPath -Pattern ('^\s*' + $Name + '\s*=\s*"([^"]*)"')
    if ($m) { return $m.Matches[0].Groups[1].Value }
    return $null
}

if (-not $AccessId)     { $AccessId     = Get-ConfigValue 'TUYA_ACCESS_ID' }
if (-not $AccessSecret) { $AccessSecret = Get-ConfigValue 'TUYA_ACCESS_SECRET' }
if (-not $DeviceId)     { $DeviceId     = Get-ConfigValue 'DEVICE_ID' }
if (-not $Region)       { $Region       = Get-ConfigValue 'TUYA_REGION' }
if (-not $Region)       { $Region       = 'us' }

# Nomes como aparecem no console, para quem copia de la em vez de usar o codigo.
$regionAliases = @{
    'western america'  = 'us'
    'america oeste'    = 'us'
    'eastern america'  = 'us-e'
    'america leste'    = 'us-e'
    'central europe'   = 'eu'
    'europa central'   = 'eu'
    'western europe'   = 'eu-w'
    'europa ocidental' = 'eu-w'
    'china'            = 'cn'
    'india'            = 'in'
}

$normalized = $Region.ToLower().Trim()
$normalized = ($normalized -replace '\s*data\s*center\s*$', '').Trim()
if ($regionAliases.ContainsKey($normalized)) { $normalized = $regionAliases[$normalized] }

if (-not $endpoints.ContainsKey($normalized)) {
    throw "Region invalida: '$Region'. Use uma de: $($endpoints.Keys -join ', ')"
}
$Region = $normalized

if (-not $AccessId)  { $AccessId  = Read-Host 'Access ID do projeto de nuvem' }
if (-not $DeviceId -and -not $List) { $DeviceId = Read-Host 'Device ID da estacao' }
if (-not $AccessSecret) {
    $secure = Read-Host 'Access Secret (nao e'' exibido)' -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try   { $AccessSecret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

if (-not $AccessId -or -not $AccessSecret) {
    throw 'Access ID e Access Secret sao obrigatorios.'
}
if (-not $DeviceId -and -not $List) {
    throw 'Informe o Device ID, ou use -List para ver os dispositivos do projeto.'
}

$base = $endpoints[$Region]

# ------------------------------------------------------------- assinatura

# A Tuya assina cada requisicao com HMAC-SHA256. A string assinada e':
#
#   client_id + [access_token] + t + [nonce] + stringToSign
#
# onde stringToSign = METODO \n SHA256(corpo) \n cabecalhos \n url
#
# O access_token entra a partir da segunda chamada: a primeira e' justamente a
# que o obtem. Nao usamos nonce nem cabecalhos assinados, entao esses trechos
# ficam vazios (mas a quebra de linha dos cabecalhos permanece).

function Get-Sha256Hex([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))
    } finally { $sha.Dispose() }
    return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Get-HmacHex([string]$Secret, [string]$Message) {
    $hmac = New-Object System.Security.Cryptography.HMACSHA256
    try {
        $hmac.Key = [Text.Encoding]::UTF8.GetBytes($Secret)
        $bytes = $hmac.ComputeHash([Text.Encoding]::UTF8.GetBytes($Message))
    } finally { $hmac.Dispose() }
    return (($bytes | ForEach-Object { $_.ToString('X2') }) -join '')
}

$emptyBodyHash = Get-Sha256Hex ''

function Invoke-TuyaGet([string]$Path, [string]$Token) {
    $t = [string][DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $stringToSign = "GET`n$emptyBodyHash`n`n$Path"
    $sign = Get-HmacHex $AccessSecret ($AccessId + $Token + $t + $stringToSign)

    $headers = @{
        'client_id'    = $AccessId
        'sign'         = $sign
        't'            = $t
        'sign_method'  = 'HMAC-SHA256'
    }
    if ($Token) { $headers['access_token'] = $Token }

    $resp = Invoke-RestMethod -Method Get -Uri ($base + $Path) -Headers $headers

    if (-not $resp.success) {
        $msg = "$($resp.msg) (code $($resp.code))"
        # Os enganos mais comuns tem diagnostico proprio: o erro cru da Tuya
        # nao ajuda muito.
        switch ([string]$resp.code) {
            '1004' { $msg += "`n  -> assinatura invalida: confira o Access Secret." }
            '1106' { $msg += "`n  -> sem permissao: o dispositivo pertence a este projeto? A conta do app esta vinculada (Devices -> Link App Account)?" }
            '1101' { $msg += "`n  -> Access ID invalido para este data center." }
            '2007' { $msg += "`n  -> data center errado: o projeto vive em outra regiao (-Region)." }
            '1010' { $msg += "`n  -> token expirado; rode de novo." }
        }
        throw "Tuya recusou $Path : $msg"
    }
    return $resp.result
}

# ---------------------------------------------------------------- consulta

Write-Output "Data center: $Region ($base)"
Write-Output "Device:      $(if ($List) { '(listando todos)' } else { $DeviceId })`n"

$token = (Invoke-TuyaGet '/v1.0/token?grant_type=1' '').access_token

if ($List) {
    # Lista os dispositivos das contas de aplicativo vinculadas ao projeto
    # (Cloud -> Devices -> Link App Account). E' por aqui que se descobre o novo
    # device ID depois de readicionar a estacao. A categoria da estacao e' 'qxj'.
    $devices = (Invoke-TuyaGet '/v1.0/iot-01/associated-users/devices?page_size=100' $token).devices
    if (-not $devices) { throw 'Nenhum dispositivo associado. A conta do app esta vinculada ao projeto?' }

    $devices |
        Sort-Object category, name |
        Select-Object @{n='id';e={$_.id}},
                      @{n='nome';e={$_.name}},
                      @{n='cat';e={$_.category}},
                      @{n='online';e={$_.online}},
                      @{n='local_key';e={$_.local_key}} |
        Format-Table -AutoSize | Out-String -Width 200 | Write-Output

    Write-Output "Total: $($devices.Count) dispositivo(s). A estacao meteorologica e' a de categoria 'qxj'."
    return
}

$device = Invoke-TuyaGet "/v1.0/devices/$DeviceId" $token

if (-not $device.local_key) {
    throw 'A resposta nao trouxe local_key. O device ID e'' de um dispositivo Tuya valido?'
}

Write-Output "Nome:         $($device.name)"
Write-Output "Produto:      $($device.product_name)"
Write-Output "Categoria:    $($device.category)"
Write-Output "Online:       $($device.online)"
Write-Output "IP (externo): $($device.ip)"
Write-Output ''
Write-Output "local_key:    $($device.local_key)"
Write-Output ''
Write-Output 'Copie exatamente, sem aparar nada: sao 16 caracteres e costumam incluir'
Write-Output 'pontuacao. Vai em LOCAL_KEY - estacaolocal/config.h (firmware) e'
Write-Output 'tools/config.py (scripts de diagnostico).'
Write-Output ''
Write-Output 'O IP acima e'' o da sua internet, nao o da estacao na LAN.'
Write-Output 'Para esse: tools/tuya_lan_identify.ps1'
