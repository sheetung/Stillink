$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$packageVersion = (Select-String -LiteralPath 'pyproject.toml' -Pattern '^version = "([^"]+)"$').Matches.Groups[1].Value
if ($packageVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid package version' }
$zipPath = "dist/StillinkServices-v$packageVersion-win.zip"
uv sync --frozen --extra windows --group dev
if ($LASTEXITCODE -ne 0) { throw 'Dependency install failed' }
uv run --frozen --extra windows pyinstaller --noconfirm --clean --onefile --windowed --name StillinkServices --specpath build windows_app.py
if ($LASTEXITCODE -ne 0) { throw 'EXE build failed' }
Copy-Item -LiteralPath 'WINDOWS.md' -Destination 'dist/使用说明.md'
$checkFile = Join-Path $PSScriptRoot 'build/packaged-check.json'
if (Test-Path -LiteralPath $checkFile) { Remove-Item -LiteralPath $checkFile }
$checkProcess = Start-Process -FilePath (Join-Path $PSScriptRoot 'dist/StillinkServices.exe') -ArgumentList '--packaged-check', ('"' + $checkFile + '"') -WindowStyle Hidden -PassThru
if (-not $checkProcess.WaitForExit(30000)) { $checkProcess.Kill(); throw 'Packaged service check timed out' }
if ($checkProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $checkFile)) { throw 'Packaged service check failed' }
if ((Get-Content -Raw -LiteralPath $checkFile | ConvertFrom-Json).status -ne 'ok') { throw 'Packaged service is unhealthy' }
Compress-Archive -Path 'dist/StillinkServices.exe','dist/使用说明.md' -DestinationPath $zipPath -Force
Write-Host "Built $zipPath"
