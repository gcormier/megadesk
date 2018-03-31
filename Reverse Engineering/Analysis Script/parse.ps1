$csvInputFile = "C:\temp\up-down-downlimit-limit.csv"
$csvOutputFile = "C:\temp\up-down-downlimit-limit-parsed.csv"

$out = @()

"Parsing file..."

$csv = Import-csv -path $csvInputFile

$inPacket = $False
$debugLineNumber = 0


foreach($csvLine in $csv)
{
    $val = $csvLine.'Decoded Protocol Result'

    if ($val -eq "Protected ID: 18" -or $val -eq "Protected ID: 8" -or $val -eq "Protected ID: 9")
    {
        $inPacket = $true
        $deviceID = [byte]$val.Substring(14)
    }
    elseif ($val.Substring(0, 6) -eq "Data 0" -and $inPacket)
    {
        $b0 = [byte]$val.substring(8)
    }
    elseif ($val.Substring(0, 6) -eq "Data 1" -and $inPacket)
    {
        $b1 = [byte]$val.substring(8)
    }
    elseif ($val.Substring(0, 6) -eq "Data 2" -and $inPacket)
    {
        $tempCmd = [byte]$val.substring(8)
        $cmd = switch ($tempCmd)
        {
            252 { "252 - Idle"}
            134 { "134 - Raise"}
            133 { "133 - Lower"}
            135 { "135 - Fine Move"}
            132 { "132 - ?Finalize?"}
            2   { "2 - Motor Busy"}
            3   { "3 - Motor Busy (Fine)"}
            96  { "96 - Motor Idle"}
            196 { "196 - Pre-move"}
          default { $tempCmd }
        }
        
    }
    elseif ($val -eq "Header Break" -and $inPacket)
    {
        $inPacket = $false
        
        [uint16] $height = 0
        $height += $b1
        $height = $height -shl 8
        $height += $b0

        #"PID $deviceID CMD $cmd HEIGHT $height"
        #$out += $deviceID, $cmd, $height
        $out += New-Object PsObject -property @{
            'PID' = $deviceID
            'CMD' = $cmd
            'HEIGHT' = $height
        }
    }




}

$out | Export-Csv -Path $csvOutputFile -NoTypeInformation

"Done!"
