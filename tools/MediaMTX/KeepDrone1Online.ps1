# Demo helper: refreshes local Drone 1 telemetry so the backend keeps it online.
$Body = @{
    position = @(0, 0, -10)
    velocity = @(0, 0, 0)
    q = @(1, 0, 0, 0)
    battery = 85
    gps_lat = 39.9042
    gps_lon = 116.4074
    gps_alt = 50
    gps_fix = $true
    arming_state = 2
    nav_state = 14
} | ConvertTo-Json -Compress

while ($true) {
    try {
        Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/inject' `
            -ContentType 'application/json' -Body $Body | Out-Null
    }
    catch {
        Write-Warning $_.Exception.Message
    }
    Start-Sleep -Seconds 1
}
