# Gera estacaolocal/certificates.h a partir do armazenamento do Windows.
#
# O ESP32 nao tem uma lista de CAs embutida: precisamos compilar as raizes
# confiaveis dentro do firmware. Este script extrai um punhado de raizes
# publicas amplamente usadas e as concatena num unico PEM, que o mbedTLS
# consegue parsear de uma vez via WiFiClientSecure::setCACert().
#
# Uso:  powershell -ExecutionPolicy Bypass -File tools/gen_certificates.ps1

$ErrorActionPreference = 'Stop'

# Raizes candidatas. Nao sabemos qual CA cada host usa (e ela pode mudar sem
# aviso), entao embutimos as principais. Custo: ~1,5 KB de flash por raiz.
#
# O unico host HTTPS deste firmware e' o Weather Underground - a estacao e' lida
# em texto binario na LAN, sem TLS.
#
# Emissores de *.wunderground.com confirmados via Certificate Transparency
# (api.certspotter.com): DigiCert G2/G3, Amazon RSA 2048 M04 e Sectigo Public
# Server Auth CA OV R36. Ele alterna entre eles sem aviso, entao todas as raizes
# correspondentes ficam embutidas para o firmware sobreviver as rotacoes.
$wanted = @(
    'DigiCert Global Root CA',
    'DigiCert Global Root G2',
    'DigiCert Global Root G3',
    'DigiCert High Assurance EV Root CA',
    'USERTrust RSA Certification Authority',         # Sectigo
    'AAA Certificate Services',                      # Sectigo/Comodo
    'Baltimore CyberTrust Root',
    'ISRG Root X1',                                  # Let's Encrypt
    'Amazon Root CA 1',
    'GlobalSign Root CA'
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$outFile = Join-Path $repoRoot 'estacaolocal\certificates.h'

$roots = Get-ChildItem Cert:\LocalMachine\Root

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// Gerado por tools/gen_certificates.ps1 - nao editar a mao.')
[void]$sb.AppendLine('//')
[void]$sb.AppendLine('// Raizes de CA embutidas no firmware. O ESP32 nao possui um repositorio')
[void]$sb.AppendLine('// de CAs proprio, e usar setInsecure() exporia a WU_STATION_KEY, que trafega')
[void]$sb.AppendLine('// em texto claro na query string do Weather Underground.')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#ifndef CERTIFICATES_H')
[void]$sb.AppendLine('#define CERTIFICATES_H')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const char ROOT_CA_BUNDLE[] PROGMEM =')

$found = @()
foreach ($name in $wanted) {
    $cert = $roots | Where-Object { $_.Subject -match [regex]::Escape($name) } | Select-Object -First 1
    if (-not $cert) {
        Write-Warning "nao encontrada no armazenamento do Windows: $name"
        continue
    }
    $found += $name

    $b64 = [Convert]::ToBase64String($cert.RawData)
    [void]$sb.AppendLine("  // $name")
    [void]$sb.AppendLine('  "-----BEGIN CERTIFICATE-----\n"')
    for ($i = 0; $i -lt $b64.Length; $i += 64) {
        $len = [Math]::Min(64, $b64.Length - $i)
        [void]$sb.AppendLine('  "' + $b64.Substring($i, $len) + '\n"')
    }
    [void]$sb.AppendLine('  "-----END CERTIFICATE-----\n"')
}

# Raizes que nao estao no armazenamento do Windows entram como arquivos .pem
# em tools/extra_certs/. E' o caso da Starfield Services Root G2, que assina de
# forma cruzada a Amazon Root CA 1 e nao consta no armazenamento do Windows.
$extraDir = Join-Path $PSScriptRoot 'extra_certs'
if (Test-Path $extraDir) {
    foreach ($pem in Get-ChildItem $extraDir -Filter '*.pem') {
        [void]$sb.AppendLine("  // $($pem.BaseName) (tools/extra_certs)")
        foreach ($line in Get-Content $pem.FullName) {
            $trimmed = $line.Trim()
            if ($trimmed.Length -eq 0) { continue }
            [void]$sb.AppendLine('  "' + $trimmed + '\n"')
        }
        $found += $pem.BaseName
    }
}

[void]$sb.AppendLine('  ;')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#endif  // CERTIFICATES_H')

if ($found.Count -eq 0) {
    throw 'Nenhuma raiz encontrada. Abortando sem escrever o arquivo.'
}

Set-Content -Path $outFile -Value $sb.ToString() -Encoding utf8

Write-Output "Escrito: $outFile"
Write-Output "Raizes embutidas ($($found.Count)):"
$found | ForEach-Object { Write-Output "  - $_" }
