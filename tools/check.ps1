<#
.SYNOPSIS
  Builds and runs every bare_calc host check: engine, CAS, exact arithmetic,
  editor, fonts, renders, apps, store, Math Box and the reference ledger.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1
  ... -File tools/check.ps1 -Only engine,cas     # named stages
  ... -File tools/check.ps1 -Clean               # recompile every object

  One line per stage: PASS / FAIL / SKIP. Full output of each stage is in
  .build/check/<stage>.log; render review sheets (PNG) in
  .build/check/renders/<stage>/. Exit code = number of FAIL stages.
#>
param(
  [string[]]$Only,
  [switch]$Clean,
  [int]$Jobs = [Environment]::ProcessorCount,
  [string]$Compiler = 'C:/msys64/mingw64/bin/g++.exe',
  [string]$Python   # default: $env:BARE_CALC_PYTHON, else the nearest */.venv above this folder
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$out = Join-Path $repo '.build\check'
$objRoot = Join-Path $out 'obj'
$bin = Join-Path $out 'bin'
if ($Clean -and (Test-Path -LiteralPath $objRoot)) { Remove-Item -Recurse -Force -LiteralPath $objRoot }
New-Item -ItemType Directory -Force -Path $objRoot, $bin | Out-Null
$env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
$env:PYTHONIOENCODING = 'utf-8'
$gcc = Join-Path (Split-Path -Parent $Compiler) 'gcc.exe'
$utf8 = New-Object Text.UTF8Encoding $false
if (-not $Python) { $Python = $env:BARE_CALC_PYTHON }
if (-not $Python) {
  for ($dir = $repo; $dir -and -not $Python; $dir = Split-Path -Parent $dir) {
    $Python = Get-ChildItem -LiteralPath $dir -Directory -ErrorAction SilentlyContinue |
      ForEach-Object { Join-Path $_.FullName '.venv\Scripts\python.exe' } |
      Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  }
}
$hasPython = $Python -and (Test-Path -LiteralPath $Python)

# --- compiling -----------------------------------------------------------------
# Objects are cached per flag set and rebuilt when the source or any header it
# included (g++ -MMD depfile) is newer. Compiles run $Jobs at a time.
function Src([string[]]$names) {
  foreach ($name in $names) {
    $found = @(Get-ChildItem -LiteralPath "$repo/src" -Recurse -File -Filter "$name.cpp")
    if ($found.Count -ne 1) { throw "Expected one source for $name; found $($found.Count)" }
    $found[0].FullName
  }
}
function ObjPath([string]$src, [string[]]$flags) {
  $key = ($flags -join "`n")
  $sha = [Security.Cryptography.SHA1]::Create().ComputeHash($utf8.GetBytes($key))
  $dir = Join-Path $objRoot ([BitConverter]::ToString($sha).Replace('-', '').Substring(0, 10).ToLower())
  $full = [IO.Path]::GetFullPath($src)
  $rel = if ($full.StartsWith("$repo\", 'OrdinalIgnoreCase')) { $full.Substring($repo.Length + 1) } else { $full.Replace(':', '') }
  Join-Path $dir "$rel.o"
}
function Stale([string]$src, [string]$obj) {
  if (-not (Test-Path -LiteralPath $obj)) { return $true }
  $built = (Get-Item -LiteralPath $obj).LastWriteTimeUtc
  if ((Get-Item -LiteralPath $src).LastWriteTimeUtc -gt $built) { return $true }
  $dep = "$obj.d"
  if (-not (Test-Path -LiteralPath $dep)) { return $true }
  $text = [IO.File]::ReadAllText($dep) -replace '\\\r?\n', ' '
  $body = $text.Substring($text.IndexOf(': ') + 2)
  foreach ($file in ($body -split '(?<!\\)\s+' | Where-Object { $_ })) {
    $path = $file.Replace('\ ', ' ')
    if (-not (Test-Path -LiteralPath $path)) { return $true }
    if ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $built) { return $true }
  }
  $false
}
# Compiles `sources` with `flags` (C files get -std=c99 and their own flags
# from $cFlags), links .build/check/bin/<name>.exe and returns its path. Any
# compiler error throws with the compiler output.
function Build([string]$name, [string[]]$flags, [string[]]$sources, [string[]]$cFlags = @()) {
  $objs = @(); $pending = @()
  foreach ($src in $sources) {
    $isC = $src.EndsWith('.c')
    $cc = if ($isC) { $gcc } else { $Compiler }
    $f = if ($isC) { @('-std=c99') + $cFlags } else { @('-std=c++17') + $flags }
    $obj = ObjPath $src (@($cc) + $f)
    $objs += $obj
    if (Stale $src $obj) { $pending += , @($cc, $f, $src, $obj) }
  }
  $running = @()
  $errors = New-Object Text.StringBuilder
  $queue = New-Object Collections.Queue
  foreach ($p in $pending) { $queue.Enqueue($p) }
  while ($queue.Count -or $running.Count) {
    while ($queue.Count -and $running.Count -lt $Jobs) {
      $cc, $f, $src, $obj = $queue.Dequeue()
      [void][IO.Directory]::CreateDirectory((Split-Path -Parent $obj))
      $psi = New-Object Diagnostics.ProcessStartInfo $cc
      $psi.Arguments = ((@($f) + @('-MMD', '-MF', "$obj.d", '-c', $src, '-o', $obj)) | ForEach-Object { '"' + $_ + '"' }) -join ' '
      $psi.UseShellExecute = $false; $psi.RedirectStandardError = $true; $psi.RedirectStandardOutput = $true
      $proc = [Diagnostics.Process]::Start($psi)
      $running += , @($proc, $proc.StandardError.ReadToEndAsync(), $src)
    }
    $still = @()
    foreach ($r in $running) {
      if ($r[0].HasExited) {
        $r[0].WaitForExit()
        if ($r[0].ExitCode -ne 0) { [void]$errors.AppendLine("$($r[2]):`n$($r[1].Result)") }
      } else { $still += , $r }
    }
    $running = $still
    if ($running.Count) { Start-Sleep -Milliseconds 50 }
  }
  if ($errors.Length) { throw "compile failed`n$errors" }
  $exe = Join-Path $bin "$name.exe"
  $needLink = -not (Test-Path -LiteralPath $exe)
  if (-not $needLink) {
    $linked = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
    $needLink = @($objs | Where-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $linked }).Count -gt 0
  }
  if ($needLink) {
    $log = & $Compiler @objs -o $exe -static 2>&1
    if ($LASTEXITCODE -ne 0) { throw "link failed`n$($log -join "`n")" }
  }
  $exe
}
# Runs a process from the project root; a non-zero exit throws with its output.
function Run([string]$exe) {
  Push-Location $repo
  try { $log = & $exe @args 2>&1; $code = $LASTEXITCODE } finally { Pop-Location }
  Say ($log -join "`n")
  if ($code -ne 0) { throw "$([IO.Path]::GetFileName($exe)) exited with $code" }
  $log
}
function Py { if (-not $hasPython) { throw 'SKIP: no Python (pass -Python)' }; Run $Python @args }
function Say([string]$text) { [IO.File]::AppendAllText($script:log, $text + "`n", $utf8) }
function Frames([string]$name) {
  $dir = Join-Path $out "$name-frames"
  [void][IO.Directory]::CreateDirectory($dir)
  Remove-Item -Force -Path "$dir\*.pgm", "$dir\*.pbm" -ErrorAction SilentlyContinue
  $dir
}
function Sheet([string]$frames, [string]$name) {
  if ($hasPython) { Py "$repo/tools/render/render_sheet.py" $frames (Join-Path $out "renders\$name") | Out-Null }
}

# --- shared source lists and flags ---------------------------------------------
$gfx = "$repo/lib/Adafruit_GFX"
$json = "$repo/lib/ArduinoJson/src"
$decNumber = @("$repo/lib/decNumber/decNumber.c", "$repo/lib/decNumber/decContext.c")
$decFlags = @('-O2', '-DDECNUMDIGITS=40', '-I', "$repo/lib/decNumber")
$hostFlags = @('-O2', '-DARDUINO=100', '-w', '-I', "$repo/test/render_fakes", '-I', $gfx, '-I', "$repo/src", '-I', "$repo/include",
  '-I', "$repo/lib/decNumber", '-I', $json, '-I', "$repo/test", '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0',
  '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0', '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', '-DARDUINOJSON_ENABLE_PROGMEM=0')
$engineFlags = @('-O2', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-I', "$repo/src", '-I', "$repo/lib/decNumber")
$renderSrc = (Src @('calculator_render', 'calculator_editor', 'calculator_document', 'calculator_layout',
  'calculator_font', 'calculator_menu', 'calculator_icons', 'calculator_engine', 'calculator_complex', 'calculator_basen',
  'calculator_decimal', 'calculator_bigint', 'calculator_exact', 'calculator_session', 'calculator_presentation',
  'calculator_linear', 'canvas_text', 'ui_kit'))
$engineSrc = Src @('calculator_decimal', 'calculator_bigint', 'calculator_exact', 'calculator_engine', 'calculator_complex',
  'calculator_basen', 'calculator_aggregate', 'calculator_polynomial')
$casSrc = Src @('calculator_cas', 'calculator_cas_expr', 'calculator_cas_canonical', 'calculator_cas_pattern', 'calculator_cas_poly',
  'calculator_cas_simplify', 'calculator_cas_diff', 'calculator_cas_integrate', 'calculator_cas_partfrac', 'calculator_cas_solve', 'calculator_cas_print')
$appSrc = @("$gfx/Adafruit_GFX.cpp") + $renderSrc + $casSrc + (Src @('calculator_quick', 'calculator_ratio',
  'calculator_table', 'calculator_spreadsheet', 'calculator_spreadsheet_math', 'calculator_probability', 'calculator_statistics_math',
  'calculator_statistics', 'calculator_distribution', 'calculator_grid', 'calculator_job', 'calculator_aggregate', 'calculator_polynomial',
  'calculator_matrix', 'calculator_equation', 'calculator_store', 'calculator_calculate', 'calculator_app', 'calculator_home',
  'calculator_clock', 'calculator_mathbox')) + @("$repo/test/calculator_platform_fake.cpp") + $decNumber

# --- stages --------------------------------------------------------------------
$stages = [ordered]@{
  # The calculator is offline: nothing in src/ or include/ may mention a
  # network or connectivity feature. Comments count too.
  isolation   = {
    $pattern = '(?i)wi-?fi|wlan|network|internet|server|https?:|\burl\b|socket|\bqr\b|qrcode|bluetooth|\bble\b|\bota\b|upload|download'
    $hits = foreach ($file in Get-ChildItem -LiteralPath "$repo/src", "$repo/include" -Recurse -File) {
      $n = 0
      foreach ($line in [IO.File]::ReadLines($file.FullName)) {
        $n++
        if ($line -match $pattern) { "$($file.FullName.Substring($repo.Length + 1)):${n}: $($line.Trim())" }
      }
    }
    if ($hits) { Say ($hits -join "`n"); throw "$(@($hits).Count) forbidden reference(s) in src/ or include/" }
    $files = @(Get-ChildItem -LiteralPath "$repo/src", "$repo/include" -Recurse -File).Count
    "$files source files, no connectivity references"
  }
  editor      = { Run (Build editor @('-Wall', '-Wextra', '-Werror', '-I', "$repo/src") @("$repo/src/input/expression_editor.cpp", "$repo/test/expression_editor_test.cpp")) | Out-Null }
  exact       = {
    $src = (Src @('calculator_decimal', 'calculator_bigint', 'calculator_exact')) + "$repo/test/calculator_exact_test.cpp" + $decNumber
    Run (Build exact @('-O2', '-Wall', '-Wextra', '-Werror', '-I', "$repo/src", '-I', "$repo/lib/decNumber") $src $decFlags) | Out-Null
    # Optimized output can hide signed-overflow UB (e.g. -INT64_MIN).
    Run (Build exact-trap @('-O0', '-ftrapv', '-I', "$repo/src", '-I', "$repo/lib/decNumber") $src $decFlags) | Out-Null
  }
  engine      = { Run (Build engine $engineFlags ($engineSrc + $casSrc + "$repo/test/calculator_engine_test.cpp" + $decNumber) $decFlags) | Out-Null }
  cas         = { Run (Build cas $engineFlags ($engineSrc + $casSrc + "$repo/test/calculator_cas_test.cpp" + $decNumber) $decFlags) | Out-Null }
  home        = {
    $e = Build home @('-O2', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-I', "$repo/src") `
      @("$repo/src/calculator/ui/calculator_home.cpp", "$repo/test/calculator_home_test.cpp")
    Run $e "$repo/test/calculator_references" | Select-Object -Last 1
  }
  font        = {
    Py "$repo/tools/checks/check_calculator_menu_labels.py" | Out-Null
    $frames = Frames font
    Run (Build font $hostFlags @("$gfx/Adafruit_GFX.cpp", "$repo/src/calculator/ui/calculator_font.cpp", "$repo/test/calculator_font_test.cpp")) "$frames/font-sheet.pbm" | Out-Null
    # Frozen generated fonts: never hand-edited, never re-baked by accident.
    # Hashed with LF line endings, so any checkout's line-ending setting agrees.
    $frozen = @{ 'src/calculator/assets/font_calculator_math.h' = 'A15EFCD8984883D90A56E174F8BA53C25580B9F2C9D85A88F2207D2DC2D422FB'
                 'src/ui/font_classwiz15.h' = 'F9466D6674597BC8C53345C5A03DF19E7C01F635C36F0575965A3F5DA8694BBA' }
    foreach ($rel in $frozen.Keys) {
      $text = [IO.File]::ReadAllText("$repo/$rel").Replace("`r`n", "`n")
      $hash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '')
      if ($hash -ne $frozen[$rel]) { throw "$rel changed: $hash" }
    }
    'menu labels fit; both generated fonts match their frozen hashes'
  }
  uikit       = { Run (Build canvas_text_lifetime $hostFlags @("$gfx/Adafruit_GFX.cpp", "$repo/test/canvas_text_lifetime_test.cpp")) | Select-Object -Last 1 }
  render      = {
    $frames = Frames calculator
    $e = Build render $hostFlags (@("$gfx/Adafruit_GFX.cpp") + $renderSrc + "$repo/test/calculator_render_test.cpp" + $decNumber) $decFlags
    $log = Run $e check $frames
    foreach ($gate in 'gray', 'p9', 'p10') { Py "$repo/tools/checks/check_calculator_$gate.py" $frames | Out-Null }
    Sheet $frames calculator
    ($log | Select-String 'calculator render:' | Select-Object -Last 1).Line
  }
  stress      = {
    Run (Build stress $hostFlags (@("$gfx/Adafruit_GFX.cpp") + (Src @('calculator_editor', 'calculator_document',
      'calculator_layout', 'calculator_font')) + "$repo/test/calculator_editor_stress_test.cpp")) 40 200 | Select-Object -Last 1
  }
  apps        = {
    Run (Build store $hostFlags ($appSrc + "$repo/test/calculator_store_test.cpp") $decFlags) | Out-Null
    Run (Build apps $hostFlags ($appSrc + "$repo/test/calculator_apps_test.cpp") $decFlags) | Out-Null
    Run (Build calculator_job_watchdog $hostFlags ($appSrc + "$repo/test/calculator_job_watchdog_test.cpp") $decFlags) | Out-Null
  }
  spreadsheet = {
    $frames = Frames spreadsheet
    Run (Build spreadsheet ($hostFlags + '-DCALCULATOR_REFERENCE_TEST') ($appSrc + "$repo/test/calculator_spreadsheet_test.cpp") $decFlags) $frames | Out-Null
    Sheet $frames spreadsheet
    Py "$repo/tools/render/review_spreadsheet.py" $frames | Select-Object -Last 1
  }
  mathbox     = {
    $frames = Frames mathbox
    Run (Build mathbox $hostFlags ($appSrc + "$repo/test/calculator_mathbox_test.cpp") $decFlags) $frames | Select-Object -Last 1
    Sheet $frames mathbox
  }
  reference   = {
    # CALCULATOR_REFERENCE_TEST changes calculator headers: its own object set.
    $e = Build reference ($hostFlags + '-DCALCULATOR_REFERENCE_TEST') ($appSrc + "$repo/test/calculator_reference_test.cpp") $decFlags
    Py "$repo/tools/checks/run_calculator_references.py" $e "$repo/test/reference_cases" | Select-Object -Last 1
  }
  # The Arduino platform starting point compiles against a minimal core.
  platform    = {
    $flags = @('-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', "$repo/test/platform_fakes", '-I', "$repo/src")
    $obj = Join-Path $out 'calculator_platform_arduino.o'
    $log = & $Compiler @flags -c "$repo/src/platform/calculator_platform_arduino.cpp" -o $obj 2>&1
    if ($LASTEXITCODE -ne 0) { Say ($log -join "`n"); throw 'calculator_platform_arduino.cpp does not compile' }
    'calculator_platform_arduino.cpp compiles (not linked or run)'
  }
}

$failed = 0
foreach ($name in $stages.Keys) {
  if ($Only -and $Only -notcontains $name) { continue }
  $script:log = Join-Path $out "$name.log"
  [IO.File]::WriteAllText($script:log, '', $utf8)
  $watch = [Diagnostics.Stopwatch]::StartNew()
  try {
    $note = & $stages[$name]
    $note = @($note | Where-Object { $_ }) -join '; '
    Write-Host ('PASS {0,-12} {1,5:n1}s  {2}' -f $name, $watch.Elapsed.TotalSeconds, $note)
  } catch {
    $message = $_.Exception.Message
    Say $message
    if ($message.StartsWith('SKIP:')) { Write-Host ('SKIP {0,-12} {1}' -f $name, $message.Substring(5).Trim()) }
    else {
      $failed++
      $first = ($message -split "`n" | Where-Object { $_.Trim() } | Select-Object -First 3) -join ' | '
      Write-Host ('FAIL {0,-12} {1,5:n1}s  {2}  (log: {3})' -f $name, $watch.Elapsed.TotalSeconds, $first, $script:log)
    }
  }
}
exit $failed
