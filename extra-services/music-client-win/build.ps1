$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
uv sync --frozen
if ($LASTEXITCODE -ne 0) { throw 'uv sync failed' }
uv run --frozen pyinstaller --noconfirm --clean --onefile --windowed --name StillinkMusic --collect-submodules winrt --collect-data winrt app.py
if ($LASTEXITCODE -ne 0) { throw 'EXE build failed' }
Copy-Item -LiteralPath '停止音乐广播.vbs','config.example.yml','README.md' -Destination dist
Compress-Archive -Path dist/StillinkMusic.exe,dist/停止音乐广播.vbs,dist/config.example.yml,dist/README.md -DestinationPath dist/StillinkMusic-win.zip -Force
Write-Host 'Built dist/StillinkMusic-win.zip'
