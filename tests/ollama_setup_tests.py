"""Run the actual setup script with fake CLI/HTTP responses; never install or download."""
import os
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from zipfile import ZipFile


SCRIPT = Path(__file__).resolve().parents[1] / "resources" / "ollama_portable.ps1"


@unittest.skipUnless(os.name == "nt", "Windows setup script")
class OllamaSetupTests(unittest.TestCase):
    def run_setup(self, model, *, installed=(), fail_pull=False, runtime_missing=False, unsafe_zip=False, bad_digest=False):
        with tempfile.TemporaryDirectory(prefix="bianlingo-ollama-test-") as tmp:
            # Windows PowerShell expands an existing 8.3 directory to its long
            # name. Compare the same canonical root (runner TEMP can be short).
            root = Path(tmp).resolve()
            runtime = root / "data" / "ollama"
            data = root / "UserData"
            data.mkdir()
            state = runtime / "installed.txt"
            calls = runtime / "calls.txt"
            result = data / "result.txt"
            cli = runtime / "fake-ollama.cmd"
            # Only synthetic, validated ASCII fixture names reach this fake CLI.
            cli_content = (
                '@echo off\n'
                'echo %*>>"%~dp0calls.txt"\n'
                + ('exit /b 7\n' if fail_pull else
                   'echo %~2>>"%~dp0installed.txt"\nexit /b 0\n'))
            archive = root / "fixture.zip"
            with ZipFile(archive, "w") as zipped:
                for name in ('GO_LICENSE','CPP_HTTPLIB_LICENSE','LLAMA_CPP_LICENSE','LLAMA_CPP_VENDORS_LICENSE'):
                    zipped.writestr('lib/ollama/'+name, 'Synthetic upstream component license fixture')
                zipped.writestr("fake-ollama.cmd", cli_content)
                zipped.writestr("installed.txt", "\n".join(installed))
                if unsafe_zip: zipped.writestr("../escape.txt", "do not extract")
            digest = "0" * 64 if bad_digest else hashlib.sha256(archive.read_bytes()).hexdigest()
            if not runtime_missing:
                runtime.mkdir(parents=True)
                cli.write_text(cli_content, encoding="ascii")
                state.write_text("\n".join(installed), encoding="utf8")
            wrapper = root / "run.ps1"
            bundled_script = root / "ollama-setup.ps1"
            for target,source in [('ollama-runtime-license.txt','Ollama.txt'),('ollama-runtime-notices.txt','OllamaRuntime.txt')]:
                (root/target).write_bytes((SCRIPT.parent/'licenses'/source).read_bytes())
            # Match the executable's BOM when extracting the UTF-8 bundled resource for Windows PowerShell 5.
            # Replace the pinned production artifact hash only in this isolated fixture.
            fixture = SCRIPT.read_text(encoding="utf-8-sig").replace(
                '7afa38624fa62ee5d8293422d0ab084c1bb4603134c7f4dafce11a033b57cc19', digest)
            bundled_script.write_text(fixture, encoding="utf-8-sig")

            def quote(value):
                return "'" + str(value).replace("'", "''") + "'"

            wrapper.write_text(
                "$ErrorActionPreference = 'Stop'\n"
                "function Get-Command { throw 'Global command lookup is forbidden' }\n"
                "function Join-Path { param($Path, $ChildPath)\n"
                " if ($ChildPath -eq 'ollama.exe') { $ChildPath = 'fake-ollama.cmd' }\n"
                " Microsoft.PowerShell.Management\\Join-Path $Path $ChildPath\n}\n"
                "function Start-Process { param($FilePath, $ArgumentList, $WorkingDirectory, $WindowStyle, [switch]$PassThru, $RedirectStandardOutput, $RedirectStandardError)\n"
                f" if ($FilePath -ne {quote(cli)}) {{ throw ('Unexpected CLI path: ' + $FilePath) }}\n"
                f" if ($env:OLLAMA_MODELS -ne {quote(data / 'ollama' / 'models')}) {{ throw ('Unexpected model path: ' + $env:OLLAMA_MODELS) }}\n"
                f" if ($env:TEMP -ne {quote(data / 'Temp')}) {{ throw ('Unexpected temporary path: ' + $env:TEMP) }}\n"
                f" if ($env:USERPROFILE -ne {quote(data / 'ollama-home')}) {{ throw ('Unexpected profile path: ' + $env:USERPROFILE) }}\n"
                " if ($env:OLLAMA_HOST -ne '127.0.0.1:11435') { throw 'Unexpected portable server endpoint' }\n"
                " $p = [pscustomobject]@{Id=12345;HasExited=$false}; $p | Add-Member -MemberType ScriptMethod -Name Refresh -Value {}; return $p\n}\n"
                "function Stop-Process { param($Id, [switch]$Force, $ErrorAction) }\n"
                "function Invoke-WebRequest { param($Uri, $OutFile, [switch]$UseBasicParsing, $TimeoutSec)\n"
                f" Copy-Item -LiteralPath {quote(archive)} -Destination $OutFile\n}}\n"
                "function Invoke-RestMethod { param($Uri, $TimeoutSec, $Headers, $Method, $ContentType, $Body)\n"
                "  if ($Uri -eq 'http://127.0.0.1:11435/api/show') { return [pscustomobject]@{license='Synthetic model license'} }\n"
                "  if ($Uri -eq 'https://api.github.com/repos/ollama/ollama/releases/latest') {\n"
                f"    return [pscustomobject]@{{assets=@([pscustomobject]@{{name='ollama-windows-amd64.zip';digest='sha256:{digest}';browser_download_url='https://github.com/ollama/ollama/releases/download/test/ollama-windows-amd64.zip'}})}}\n  }}\n"
                "  if ($Uri -eq 'http://127.0.0.1:11435/api/version') {\n"
                "    return [pscustomobject]@{version='test'}\n  }\n"
                "  if ($Uri -ne 'http://127.0.0.1:11435/api/tags') { throw 'Unexpected network request' }\n"
                f"  $models = @(Get-Content -LiteralPath {quote(state)} | Where-Object {{ $_ }} | ForEach-Object {{\n"
                "    $name = if (($_ -split '/')[-1].Contains(':')) { $_ } else { \"${_}:latest\" }\n"
                "    [pscustomobject]@{name=$name}\n  })\n"
                "  return [pscustomobject]@{models=$models}\n}\n"
                f"& {quote(bundled_script)} -Model {quote(model)} -Root {quote(root)} -Result {quote(result)}\n"
                "exit $LASTEXITCODE\n",
                encoding="utf-8-sig")
            completed = subprocess.run(
                ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)],
                capture_output=True, timeout=30)
            self.assertFalse((root / "data" / "escape.txt").exists(), "Archive escaped staging directory")
            self.assertFalse(list((root / "data").glob("ollama-staging-*")), "Staging directory was not cleaned")
            if runtime_missing and completed.returncode==0:
                self.assertEqual((runtime/'LICENSE').read_bytes(),(SCRIPT.parent/'licenses/Ollama.txt').read_bytes())
                self.assertTrue((runtime/'lib/ollama/GO_LICENSE').is_file())
                self.assertIn(b'NVIDIA',(runtime/'PROJECT_COMPONENT_NOTICES.txt').read_bytes())
            return (completed.returncode,
                    result.read_text(encoding="utf-8-sig") if result.exists() else "",
                    calls.read_text(encoding="ascii").splitlines() if calls.exists() else [])

    def test_download_only_selected_model(self):
        for model in ("qwen3:0.6b", "qwen3:1.7b", "qwen3:4b", "library/custom-small:Q4_K_M"):
            with self.subTest(model=model):
                code, result, calls = self.run_setup(model)
                self.assertEqual((code, result, calls), (0, "OK", [f"pull {model}"]))

    def test_existing_model_skips_download(self):
        self.assertEqual(self.run_setup("qwen3:0.6b", installed=("qwen3:0.6b",)), (0, "OK", []))

    def test_implicit_latest_tag(self):
        self.assertEqual(self.run_setup("library/custom-small", installed=("library/custom-small:latest",)),
                         (0, "OK", []))
        self.assertEqual(self.run_setup("library/custom-small"),
                         (0, "OK", ["pull library/custom-small"]))

    def test_failed_pull_does_not_report_ready(self):
        code, result, calls = self.run_setup("qwen3:1.7b", fail_pull=True)
        self.assertNotEqual(code, 0)
        self.assertTrue(result.startswith("ERROR: "), result)
        self.assertEqual(calls, ["pull qwen3:1.7b"])

    def test_invalid_name_never_runs_cli(self):
        code, result, calls = self.run_setup('qwen3;exit')
        self.assertNotEqual(code, 0)
        self.assertEqual(calls, [])

    def test_portable_runtime_download_without_system_install(self):
        self.assertEqual(self.run_setup("qwen3:0.6b", runtime_missing=True), (0, "OK", ["pull qwen3:0.6b"]))

    def test_runtime_archive_traversal_rejected(self):
        code, result, calls = self.run_setup("qwen3:0.6b", runtime_missing=True, unsafe_zip=True)
        self.assertNotEqual(code, 0)
        self.assertIn("Unsafe runtime archive path", result)
        self.assertEqual(calls, [])

    def test_runtime_digest_mismatch_rejected(self):
        code, result, calls = self.run_setup("qwen3:0.6b", runtime_missing=True, bad_digest=True)
        self.assertNotEqual(code, 0)
        self.assertIn("SHA-256 mismatch", result)
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
