# Balance Test Script - Simple Version
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  ECONOMY BALANCE TEST (5 Runs @ 1000t)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$results = @()

for ($i = 1; $i -le 5; $i++) {
    Write-Host "Run $i/5..." -ForegroundColor Yellow
    
    $output = "run 1000 1000`neconomy`nquit`n" | docker run --rm -i grand-strategy-kernel:latest KernelSim 2>&1 | Out-String
    
    if ($output -match 'Total Trade Volume: ([\d.]+)') { $trade = [double]$matches[1] } else { $trade = 0 }
    if ($output -match 'Welfare: ([\d.]+)') { $welfare = [double]$matches[1] } else { $welfare = 0 }
    if ($output -match 'Inequality \(Gini\): ([\d.]+)') { $gini = [double]$matches[1] } else { $gini = 0 }
    if ($output -match 'Hardship: ([\d.]+)') { $hardship = [double]$matches[1] } else { $hardship = 0 }
    if ($output -match 'Global Development: ([\d.]+)') { $dev = [double]$matches[1] } else { $dev = 0 }
    
    $results += [PSCustomObject]@{
        Run = $i
        Trade = $trade
        Welfare = $welfare
        Gini = $gini
        Hardship = $hardship
        Dev = $dev
    }
    
    Write-Host "  Trade: $trade | Welfare: $welfare | Gini: $gini | Hard: $hardship" -ForegroundColor Gray
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "         AGGREGATE RESULTS" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

$avgTrade = ($results.Trade | Measure-Object -Average).Average
$avgWelfare = ($results.Welfare | Measure-Object -Average).Average
$avgGini = ($results.Gini | Measure-Object -Average).Average
$avgHardship = ($results.Hardship | Measure-Object -Average).Average
$avgDev = ($results.Dev | Measure-Object -Average).Average

$minTrade = ($results.Trade | Measure-Object -Minimum).Minimum
$maxTrade = ($results.Trade | Measure-Object -Maximum).Maximum
$tradeVar = ($maxTrade - $minTrade) / $avgTrade * 100

Write-Host "Trade Volume:" -ForegroundColor Cyan
Write-Host "  Average: $($avgTrade.ToString('N0'))" -ForegroundColor White
Write-Host "  Range: $($minTrade.ToString('N0')) - $($maxTrade.ToString('N0'))" -ForegroundColor Gray
Write-Host "  Variance: $($tradeVar.ToString('N1'))%" -ForegroundColor Gray
Write-Host ""

Write-Host "Welfare:" -ForegroundColor Cyan
Write-Host "  Average: $($avgWelfare.ToString('N3'))" -ForegroundColor White
$minWelfare = ($results.Welfare | Measure-Object -Minimum).Minimum
$maxWelfare = ($results.Welfare | Measure-Object -Maximum).Maximum
Write-Host "  Range: $($minWelfare.ToString('N3')) - $($maxWelfare.ToString('N3'))" -ForegroundColor Gray
Write-Host ""

Write-Host "Inequality (Gini):" -ForegroundColor Cyan
Write-Host "  Average: $($avgGini.ToString('N3'))" -ForegroundColor White
$giniStatus = if ($avgGini -lt 0.4) { "Moderate" } elseif ($avgGini -lt 0.6) { "High" } else { "Extreme" }
Write-Host "  Status: $giniStatus" -ForegroundColor $(if ($avgGini -lt 0.4) { 'Yellow' } else { 'Red' })
Write-Host ""

Write-Host "Hardship:" -ForegroundColor Cyan
Write-Host "  Average: $($avgHardship.ToString('N3'))" -ForegroundColor White
$hardStatus = if ($avgHardship -lt 0.1) { "ACCEPTABLE" } elseif ($avgHardship -lt 0.2) { "CONCERNING" } else { "CRITICAL" }
Write-Host "  Status: $hardStatus" -ForegroundColor $(if ($avgHardship -lt 0.1) { 'Green' } elseif ($avgHardship -lt 0.2) { 'Yellow' } else { 'Red' })
Write-Host ""

Write-Host "Development:" -ForegroundColor Cyan
Write-Host "  Average: $($avgDev.ToString('N3'))" -ForegroundColor White
Write-Host "  Growth: ~$((($avgDev / 1000) * 100).ToString('N3'))% per tick" -ForegroundColor Gray
Write-Host ""

Write-Host "========================================" -ForegroundColor Magenta
Write-Host "           BALANCE VERDICT" -ForegroundColor Magenta
Write-Host "========================================" -ForegroundColor Magenta
Write-Host ""

$score = 0
$tradeOK = $avgTrade -gt 20000 -and $tradeVar -lt 15
$welfareOK = $avgWelfare -gt 0.8
$hardshipOK = $avgHardship -lt 0.15
$giniOK = $avgGini -lt 0.5

if ($tradeOK) { $score++; Write-Host "[PASS] Trade System: BALANCED" -ForegroundColor Green } else { Write-Host "[FAIL] Trade System: NEEDS TUNING" -ForegroundColor Red }
if ($welfareOK) { $score++; Write-Host "[PASS] Welfare: STABLE" -ForegroundColor Green } else { Write-Host "[FAIL] Welfare: UNSTABLE" -ForegroundColor Red }
if ($hardshipOK) { $score++; Write-Host "[PASS] Hardship: MANAGEABLE" -ForegroundColor Green } else { Write-Host "[FAIL] Hardship: EXCESSIVE" -ForegroundColor Red }
if ($giniOK) { $score++; Write-Host "[PASS] Inequality: MODERATE" -ForegroundColor Green } else { Write-Host "[FAIL] Inequality: HIGH" -ForegroundColor Red }

Write-Host ""
Write-Host "Overall Score: $score/4" -ForegroundColor $(if ($score -eq 4) { 'Green' } elseif ($score -ge 3) { 'Yellow' } else { 'Red' })

$verdict = switch ($score) {
    4 { "EXCELLENT - Production ready" }
    3 { "GOOD - Minor tuning needed" }
    2 { "FAIR - Balance adjustments recommended" }
    default { "POOR - Major rebalancing required" }
}
Write-Host "Status: $verdict" -ForegroundColor $(if ($score -ge 3) { 'Green' } elseif ($score -eq 2) { 'Yellow' } else { 'Red' })
Write-Host ""
