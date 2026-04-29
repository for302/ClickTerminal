# ClickTerminal 업데이트 설치 (관리자 권한으로 실행)
$cerPath  = "D:\Dev\20_PC\ClickTerminal\_msix_extract\ClickTerminalDev_new.cer"
$msixPath = "D:\Dev\20_PC\ClickTerminal\_msix_extract\CascadiaPackage_new.msix"

Write-Host "인증서 신뢰 추가 중..."
Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\Root"
Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople"
Write-Host "  완료"

Write-Host "기존 패키지 제거 중..."
Get-AppxPackage "WindowsTerminalDev" | Remove-AppxPackage -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Write-Host "  완료"

Write-Host "새 패키지 설치 중..."
Add-AppxPackage -Path $msixPath
Write-Host "  설치 완료!"
Write-Host ""
Write-Host "시작 메뉴에서 'Windows Terminal Dev' 검색해서 실행하세요."
Write-Host ""
Write-Host "아무 키나 누르면 닫힙니다..."
Read-Host
