param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
$signature = Get-AuthenticodeSignature -LiteralPath $Path
$digest = $null
if ($null -ne $signature.SignerCertificate) {
 $digest = $signature.SignerCertificate.GetCertHashString([System.Security.Cryptography.HashAlgorithmName]::SHA256)
}
@{trusted=($signature.Status -eq 'Valid');signer_sha256=$digest} | ConvertTo-Json -Compress
