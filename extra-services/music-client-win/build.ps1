$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
uv sync --frozen
if ($LASTEXITCODE -ne 0) { throw 'uv sync failed' }
uv run --frozen pyinstaller --noconfirm --clean --onefile --windowed --name StillinkMusic --specpath build --collect-submodules winrt --collect-data winrt app.py
if ($LASTEXITCODE -ne 0) { throw 'EXE build failed' }
Copy-Item -LiteralPath 'config.example.yml','README.md' -Destination dist
Compress-Archive -Path dist/StillinkMusic.exe,dist/config.example.yml,dist/README.md -DestinationPath dist/StillinkMusic-win.zip -Force
Write-Host 'Built dist/StillinkMusic-win.zip'
