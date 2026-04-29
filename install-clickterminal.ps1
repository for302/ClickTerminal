# ClickTerminal 개발 빌드 설치 스크립트 (관리자 권한 필요)
$certPath = "$PSScriptRoot\ClickTerminalDev.cer"
$msixPath = "$PSScriptRoot\src\cascadia\CascadiaPackage\AppPackages\CascadiaPackage_0.0.1.0_x64_Test\CascadiaPackage_0.0.1.0_x64.msix"

try {
    Write-Host "1. 인증서 설치 중..."
    Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root"
    Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople"
    Write-Host "   인증서 OK"

    Write-Host "2. MSIX 패키지 설치 중..."
    Add-AppxPackage -Path $msixPath -ForceApplicationShutdown
    Write-Host "   설치 완료!"
    Write-Host ""
    Write-Host "시작 메뉴에서 'Windows Terminal Dev' 검색해서 실행하세요."
}
catch {
    Write-Host ""
    Write-Host "에러 발생:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ""
    Write-Host "관리자 권한으로 실행했나요? 파일 우클릭 → 관리자 권한으로 실행" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "아무 키나 누르면 창이 닫힙니다..."
Read-Host
