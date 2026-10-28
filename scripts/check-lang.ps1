<#
.SYNOPSIS
  Verifies that every language table of the mod carries exactly the keys of the English one:
  mod\lang\<code>.ini, and every table folder below mod\lang (such as the film descriptions),
  for all 20 languages of the game.
#>
$root = Split-Path -Parent $PSScriptRoot
$langDir = Join-Path $root "mod\lang"
function Keys($file) {
    $keys = @()
    foreach ($line in Get-Content $file -Encoding UTF8) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith(';') -or $t.StartsWith('[')) { continue }
        $eq = $t.IndexOf('=')
        if ($eq -gt 0) { $keys += $t.Substring(0, $eq).Trim().ToLowerInvariant() }
    }
    return $keys
}
$expected = @("da_DK","de_DE","en_US","es_ES","es_MX","fi_FI","fr_FR","it_IT","ja_JP","ko_KR","nb_NO","nl_NL","pl_PL","pt_BR","pt_PT","ru_RU","sv_SE","tr_TR","zh_CHS","zh_CHT")
$failed = $false
$total = 0
$dirs = @($langDir) + @(Get-ChildItem $langDir -Directory | ForEach-Object { $_.FullName })
foreach ($dir in $dirs) {
    $masterFile = Join-Path $dir "en_US.ini"
    if (-not (Test-Path $masterFile)) { Write-Host "MISSING FILE: $masterFile"; $failed = $true; continue }
    $master = Keys $masterFile
    $total += $master.Count
    foreach ($code in $expected) {
        $file = Join-Path $dir "$code.ini"
        $label = if ($dir -eq $langDir) { "$code" } else { (Split-Path -Leaf $dir) + "\$code" }
        if (-not (Test-Path $file)) { Write-Host "MISSING FILE: $label.ini"; $failed = $true; continue }
        $keys = Keys $file
        $missing = $master | Where-Object { $keys -notcontains $_ }
        $extra = $keys | Where-Object { $master -notcontains $_ }
        if ($missing.Count -gt 0) { Write-Host "$label missing: $($missing -join ', ')"; $failed = $true }
        if ($extra.Count -gt 0) { Write-Host "$label extra: $($extra -join ', ')"; $failed = $true }
    }
}
if ($failed) { Write-Host "LANG CHECK FAILED"; exit 1 } else { Write-Host "LANG CHECK OK ($total keys, $($expected.Count) languages, $($dirs.Count) tables)"; exit 0 }
