# ClickTerminal Installer EXE Builder
# Usage: powershell -ExecutionPolicy Bypass -File _build_installer.ps1

$ErrorActionPreference = 'Stop'

$root     = 'D:\Dev\20_PC\ClickTerminal'
$msixPath = "$root\_msix_extract\CascadiaPackage_new.msix"
$cerPath  = "$root\_msix_extract\ClickTerminalDev_new.cer"
$outExe   = "$root\ClickTerminal-Setup.exe"
$csc      = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'

Write-Host "[1/3] Checking files..."
if (-not (Test-Path $msixPath)) { Write-Host "MSIX not found: $msixPath"; exit 1 }
if (-not (Test-Path $cerPath))  { Write-Host "CER not found: $cerPath"; exit 1 }
$msixSz = [math]::Round((Get-Item $msixPath).Length / 1MB, 1)
$cerSz  = [math]::Round((Get-Item $cerPath).Length / 1KB, 1)
Write-Host "  MSIX: $msixSz MB"
Write-Host "  CER:  $cerSz KB"

Write-Host "[2/3] Generating C# source..."

$src = @'
using System;
using System.IO;
using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography.X509Certificates;
using System.Security.Principal;

class Setup {
    static byte[] ReadRes(string name) {
        var asm = Assembly.GetExecutingAssembly();
        using (var s = asm.GetManifestResourceStream(name)) {
            var ms = new MemoryStream();
            s.CopyTo(ms);
            return ms.ToArray();
        }
    }

    static void Main() {
        bool isAdmin = new WindowsPrincipal(WindowsIdentity.GetCurrent())
            .IsInRole(WindowsBuiltInRole.Administrator);
        if (!isAdmin) {
            var psi = new ProcessStartInfo(Assembly.GetExecutingAssembly().Location) {
                Verb = "runas", UseShellExecute = true
            };
            try { Process.Start(psi); } catch {}
            return;
        }

        string tmp = Path.Combine(Path.GetTempPath(),
            "ClickTerminalSetup_" + Path.GetRandomFileName().Replace(".", ""));
        Directory.CreateDirectory(tmp);
        string msix = Path.Combine(tmp, "CascadiaPackage_new.msix");
        string cer  = Path.Combine(tmp, "ClickTerminalDev.cer");

        try {
            Console.WriteLine("\n ClickTerminal Installer");
            Console.WriteLine(" =======================\n");

            Console.WriteLine(" [1/3] Extracting files...");
            File.WriteAllBytes(msix, ReadRes("CascadiaPackage_new.msix"));
            File.WriteAllBytes(cer,  ReadRes("ClickTerminalDev.cer"));
            Console.WriteLine("       OK");

            Console.WriteLine(" [2/3] Trusting certificate...");
            var cert = new X509Certificate2(cer);
            foreach (var storeName in new[] { StoreName.Root, StoreName.TrustedPeople }) {
                var store = new X509Store(storeName, StoreLocation.LocalMachine);
                store.Open(OpenFlags.ReadWrite);
                store.Add(cert);
                store.Close();
            }
            Console.WriteLine("       OK");

            Console.WriteLine(" [3/3] Installing ClickTerminal...");
            RunPS("Get-AppxPackage -Name 'WindowsTerminalDev' | Remove-AppxPackage -EA SilentlyContinue; Start-Sleep 1; Add-AppxPackage '" + msix + "'");
            Console.WriteLine("       OK");

            Console.WriteLine("\n Done! Launching ClickTerminal...\n");
            RunPS("Start-Process 'shell:AppsFolder\\WindowsTerminalDev_xpqk32cx38ema!App'");
        } catch (Exception ex) {
            Console.WriteLine("\n ERROR: " + ex.Message);
        } finally {
            try { Directory.Delete(tmp, true); } catch {}
        }

        Console.WriteLine("Press any key to close...");
        Console.ReadKey(true);
    }

    static int Run(string exe, string args) {
        var p = new Process {
            StartInfo = new ProcessStartInfo(exe, args) {
                CreateNoWindow = true, UseShellExecute = false,
                RedirectStandardOutput = true, RedirectStandardError = true
            }
        };
        p.Start();
        string stdout = p.StandardOutput.ReadToEnd();
        string stderr = p.StandardError.ReadToEnd();
        p.WaitForExit();
        if (p.ExitCode != 0 && (stdout + stderr).Length > 0)
            Console.WriteLine("       " + (stdout + stderr).Trim().Replace("\n", "\n       "));
        return p.ExitCode;
    }

    static void RunPS(string cmd) {
        int code = Run("powershell", "-ExecutionPolicy Bypass -NoProfile -Command \"" +
            cmd.Replace("\"", "\\\"") + "\"");
        if (code != 0)
            throw new Exception("PowerShell exited with code " + code);
    }
}
'@

$csPath = "$root\ClickTerminal-Setup.cs"
[System.IO.File]::WriteAllText($csPath, $src, [System.Text.Encoding]::UTF8)

Write-Host "[3/3] Compiling (embedding $msixSz MB MSIX)..."
$result = & $csc /target:exe /platform:x64 /optimize+ `
    "/out:$outExe" `
    "/res:$msixPath,CascadiaPackage_new.msix" `
    "/res:$cerPath,ClickTerminalDev.cer" `
    "$csPath" 2>&1
$exitCode = $LASTEXITCODE

Remove-Item $csPath -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    Write-Host "COMPILE ERROR:"
    $result | Where-Object { $_ -match 'error|warning' } | Write-Host
    exit 1
}

$sz = [math]::Round((Get-Item $outExe).Length / 1MB, 1)
Write-Host ""
Write-Host "SUCCESS: ClickTerminal-Setup.exe ($sz MB)"
Write-Host "Location: $outExe"
Write-Host ""
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
Stop-Process -Id $PID
