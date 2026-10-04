<#
.SYNOPSIS
  Two-way ESP32 Serial Monitor replacement: echoes everything the board
  sends to both the console and a log file, AND lets you type a line and
  press Enter to send it to the board (e.g. LISTEN) - so a full test run,
  both directions, can be saved and re-read later instead of copy-pasting
  from the Arduino Serial Monitor.

.PARAMETER Port
  The COM port the ESP32 is on (check Arduino IDE's Tools > Port menu, or
  Windows Device Manager > Ports (COM & LPT)).

.PARAMETER BaudRate
  Must match the sketch's Serial.begin() rate. All sketches in this project
  use 115200.

.PARAMETER LogFile
  Where to save the log. Defaults to a timestamped file in the current
  directory.

.EXAMPLE
  .\serial_logger.ps1 -Port COM9
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$Port,

  [int]$BaudRate = 115200,

  [string]$LogFile = "esp32_log_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').txt"
)

$available = [System.IO.Ports.SerialPort]::GetPortNames()
if ($available -notcontains $Port) {
  Write-Host "Warning: $Port not currently listed as available. Available ports: $($available -join ', ')" -ForegroundColor Yellow
}

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$serialPort = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, "None", 8, "One"
$serialPort.ReadTimeout = 200  # short timeout so the loop below returns quickly to also check for typed input
$serialPort.NewLine = "`n"
# Default SerialPort.Encoding is ASCII, which mangles any non-ASCII byte the
# ESP32 sends into "?" - and OpenAI's spoken-reply transcripts routinely
# contain UTF-8 multi-byte punctuation (em dashes, curly apostrophes), which
# showed up as "???" per multi-byte character without this.
$serialPort.Encoding = [System.Text.Encoding]::UTF8

try {
  $serialPort.Open()
} catch {
  Write-Host "Failed to open $Port - is it already open in the Arduino Serial Monitor? Error: $($_.Exception.Message)" -ForegroundColor Red
  exit 1
}

Write-Host "Logging $Port at $BaudRate baud to '$LogFile'."
Write-Host "Type a line (e.g. LISTEN) and press Enter to send it to the board. Press Ctrl+C to stop."
Write-Host "(Opening the port likely just reset the ESP32, same as the Arduino Serial Monitor does.)"

$inputBuffer = ""

try {
  while ($true) {
    # Read anything the board has sent (bounded by ReadTimeout, so this
    # doesn't block the keyboard-check below for more than ~200ms).
    try {
      $line = $serialPort.ReadLine()
      $timestamp = Get-Date -Format "HH:mm:ss.fff"
      $output = "[$timestamp] $line"
      Write-Host $output
      Add-Content -Path $LogFile -Value $output -Encoding utf8
    } catch [System.TimeoutException] {
      # No data right now - fall through to check for typed input.
    }

    # Forward anything typed at the console to the board, one keystroke at
    # a time, sending the accumulated line to the board on Enter.
    while ([Console]::KeyAvailable) {
      $key = [Console]::ReadKey($true)
      if ($key.Key -eq "Enter") {
        Write-Host ">> $inputBuffer" -ForegroundColor Cyan
        Add-Content -Path $LogFile -Value ">> $inputBuffer" -Encoding utf8
        $serialPort.WriteLine($inputBuffer)
        $inputBuffer = ""
      } elseif ($key.Key -eq "Backspace") {
        if ($inputBuffer.Length -gt 0) {
          $inputBuffer = $inputBuffer.Substring(0, $inputBuffer.Length - 1)
          Write-Host -NoNewline "`b `b"
        }
      } else {
        $inputBuffer += $key.KeyChar
        Write-Host -NoNewline $key.KeyChar
      }
    }
  }
} finally {
  $serialPort.Close()
  Write-Host "Port closed. Log saved to '$LogFile'."
}
