[CmdletBinding()]
param(
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$agentRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$venvRoot = Join-Path $agentRoot '.venv'
$venvPython = Join-Path $venvRoot 'Scripts\python.exe'
$requirements = Join-Path $agentRoot 'requirements.txt'

if (-not (Test-Path -LiteralPath $venvPython)) {
    & $Python -m venv $venvRoot
    if ($LASTEXITCODE -ne 0) {
        throw "创建 Windows 助手 Python 环境失败，退出代码：$LASTEXITCODE"
    }
}

& $venvPython -m pip install --disable-pip-version-check -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "安装 Windows 媒体会话依赖失败，退出代码：$LASTEXITCODE"
}

Write-Host 'Windows 助手运行环境准备完成。'
