param([string]$Label = "MODIPAD")
# Print the drive letter (e.g. "D:") of the first volume whose label matches,
# so scripts/sync_sd.bat can auto-select the SD card.
$disk = Get-CimInstance Win32_LogicalDisk |
    Where-Object { $_.VolumeName -eq $Label -and $_.DeviceID } |
    Select-Object -First 1
if ($disk) {
    Write-Output $disk.DeviceID
}
