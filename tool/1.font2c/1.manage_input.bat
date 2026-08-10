<# : font2c config wizard - interactive generator for input\*.json (double-click to run)
@echo off
setlocal
set "F2C_HOME=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t=[IO.File]::ReadAllText('%~f0',[Text.Encoding]::UTF8);Invoke-Expression $t"
set "EC=%ERRORLEVEL%"
endlocal & exit /b %EC%
#>
# =====================================================================
#  font2c 配置生成向导 (PowerShell 部分, 由上方批处理引导执行)
#  - 启动显示 input\*.json 已有配置列表: 编号查看 / b 编号构建 / a 新建
#  - 扫描 项目fonts\ / 用户 / 系统 字体目录, 分页浏览选择
#  - 逐步引导生成 font2c 的 JSON 配置 (UTF-8 无 BOM, LF 行尾)
#  文件本身必须保存为 UTF-8 (无 BOM); 批处理段只允许 ASCII 字符。
# =====================================================================
$ErrorActionPreference = 'Stop'

$script:Root = if ($env:F2C_HOME -and $env:F2C_HOME.Trim() -ne '') { $env:F2C_HOME.TrimEnd('\', '/') } else { (Get-Location).Path.TrimEnd('\', '/') }
$script:EmptyStreak = 0

function Read-Raw([string]$Prompt) {
    $v = Read-Host $Prompt
    if ($null -eq $v) { $v = '' }
    if ($v -eq '') {
        $script:EmptyStreak++
        if ($script:EmptyStreak -gt 100) { throw '输入流已结束。' }
    } else {
        $script:EmptyStreak = 0
    }
    return $v
}

function Read-Def([string]$Prompt, [string]$Default) {
    $p = $Prompt
    if ($null -ne $Default -and $Default -ne '') { $p = "$Prompt [$Default]" }
    $v = (Read-Raw $p).Trim()
    if ($v -eq '') { return $Default }
    return $v
}

function Read-YesNo([string]$Prompt, [bool]$DefaultYes) {
    $suffix = if ($DefaultYes) { '(Y/n)' } else { '(y/N)' }
    while ($true) {
        $v = (Read-Raw "$Prompt $suffix").Trim().ToLower()
        if ($v -eq '') { return $DefaultYes }
        if ($v -in 'y', 'yes', '是') { return $true }
        if ($v -in 'n', 'no', '否') { return $false }
        Write-Host '  请输入 y 或 n。' -ForegroundColor Yellow
    }
}

# 显示宽度: CJK/全角按 2 列计算, 用于对齐列表
function Get-DispWidth([string]$s) {
    if ($null -eq $s -or $s -eq '') { return 0 }
    $w = 0
    foreach ($ch in $s.ToCharArray()) {
        $c = [int]$ch
        if (($c -ge 0x1100 -and $c -le 0x115F) -or ($c -ge 0x2E80 -and $c -le 0xA4CF) -or
            ($c -ge 0xAC00 -and $c -le 0xD7A3) -or ($c -ge 0xF900 -and $c -le 0xFAFF) -or
            ($c -ge 0xFE30 -and $c -le 0xFE4F) -or ($c -ge 0xFF00 -and $c -le 0xFF60) -or
            ($c -ge 0xFFE0 -and $c -le 0xFFE6)) { $w += 2 } else { $w += 1 }
    }
    return $w
}

function Format-Cell([string]$s, [int]$Width) {
    if ($null -eq $s) { $s = '' }
    $full = Get-DispWidth $s
    if ($full -le $Width) { return $s + (' ' * ($Width - $full)) }
    $out = ''
    $w = 0
    foreach ($ch in $s.ToCharArray()) {
        $cw = Get-DispWidth ([string]$ch)
        if ($w + $cw -gt $Width - 2) { break }
        $out += $ch
        $w += $cw
    }
    return $out + '..' + (' ' * ($Width - $w - 2))
}

function ConvertTo-JsonStringLiteral([string]$s) {
    if ($null -eq $s) { $s = '' }
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    foreach ($ch in $s.ToCharArray()) {
        $code = [int]$ch
        if ($ch -eq '"') { [void]$sb.Append('\"') }
        elseif ($ch -eq '\') { [void]$sb.Append('\\') }
        elseif ($code -eq 8) { [void]$sb.Append('\b') }
        elseif ($code -eq 12) { [void]$sb.Append('\f') }
        elseif ($code -eq 10) { [void]$sb.Append('\n') }
        elseif ($code -eq 13) { [void]$sb.Append('\r') }
        elseif ($code -eq 9) { [void]$sb.Append('\t') }
        elseif ($code -lt 0x20) { [void]$sb.Append(('\u{0:X4}' -f $code)) }
        else { [void]$sb.Append($ch) }
    }
    [void]$sb.Append('"')
    return $sb.ToString()
}

function Format-UToken([int]$cp) {
    if ($cp -gt 0xFFFF) { return 'U+{0:X}' -f $cp }
    return 'U+{0:X4}' -f $cp
}

# 收集字体: 项目 fonts\ -> 用户字体目录 -> 系统字体目录, 并从注册表取显示名
function Get-FontEntries {
    $nameMap = @{}
    $skip = @('PSPath', 'PSParentPath', 'PSChildName', 'PSDrive', 'PSProvider')
    foreach ($hive in @('HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts',
                        'HKCU:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts')) {
        try { $props = Get-ItemProperty -Path $hive -ErrorAction Stop } catch { continue }
        foreach ($p in $props.PSObject.Properties) {
            if ($skip -contains $p.Name) { continue }
            $val = [string]$p.Value
            if ($val -eq '') { continue }
            $base = [IO.Path]::GetFileName($val).ToLowerInvariant()
            if ($base -eq '') { continue }
            $friendly = $p.Name -replace '\s*\((TrueType|OpenType|VGA res|All res|8514a res)\)\s*$', ''
            if (-not $nameMap.ContainsKey($base)) { $nameMap[$base] = $friendly }
        }
    }

    $sources = New-Object System.Collections.ArrayList
    [void]$sources.Add(@{ Dir = (Join-Path $script:Root 'fonts'); Tag = '项目'; NeedPath = $false })
    if ($env:LOCALAPPDATA) {
        [void]$sources.Add(@{ Dir = (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'); Tag = '用户'; NeedPath = $true })
    }
    if ($env:WINDIR) {
        [void]$sources.Add(@{ Dir = (Join-Path $env:WINDIR 'Fonts'); Tag = '系统'; NeedPath = $false })
    }

    $entries = New-Object System.Collections.ArrayList
    foreach ($srcDef in $sources) {
        if (-not (Test-Path -LiteralPath $srcDef.Dir)) { continue }
        $files = Get-ChildItem -LiteralPath $srcDef.Dir -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -match '^\.(ttf|otf|ttc)$' } | Sort-Object Name
        foreach ($f in $files) {
            $friendly = $nameMap[$f.Name.ToLowerInvariant()]
            if ($null -eq $friendly) { $friendly = '' }
            [void]$entries.Add([pscustomobject]@{
                File = $f.Name; Path = $f.FullName; Name = $friendly
                Src = $srcDef.Tag; NeedPath = $srcDef.NeedPath
            })
        }
    }
    return , $entries
}

function Get-ManualFontEntry {
    $raw = (Read-Raw '  输入字体文件名或完整路径 (留空返回列表)').Trim().Trim('"')
    if ($raw -eq '') { return $null }
    if ($raw -match '[\\/]' -or $raw -match '^[A-Za-z]:') {
        if (-not (Test-Path -LiteralPath $raw -PathType Leaf)) {
            Write-Host "  文件不存在: $raw" -ForegroundColor Yellow
            return $null
        }
        $full = (Resolve-Path -LiteralPath $raw).Path
        $base = [IO.Path]::GetFileName($full)
        $dir = [IO.Path]::GetDirectoryName($full)
        $needPath = $true
        if ($env:WINDIR -and $dir -ieq (Join-Path $env:WINDIR 'Fonts')) { $needPath = $false }
        if ($dir -ieq (Join-Path $script:Root 'fonts')) { $needPath = $false }
        if ($base -notmatch '\.(ttf|otf|ttc)$') {
            Write-Host '  警告: 扩展名不是 ttf/otf/ttc, font2c 可能无法处理。' -ForegroundColor Yellow
        }
        return [pscustomobject]@{ File = $base; Path = $full; Name = ''; Src = '手动'; NeedPath = $needPath }
    }
    $cands = New-Object System.Collections.ArrayList
    [void]$cands.Add(@{ Dir = (Join-Path $script:Root 'fonts'); Need = $false })
    if ($env:WINDIR) { [void]$cands.Add(@{ Dir = (Join-Path $env:WINDIR 'Fonts'); Need = $false }) }
    if ($env:LOCALAPPDATA) { [void]$cands.Add(@{ Dir = (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'); Need = $true }) }
    foreach ($c in $cands) {
        $p = Join-Path $c.Dir $raw
        if (Test-Path -LiteralPath $p -PathType Leaf) {
            return [pscustomobject]@{ File = $raw; Path = $p; Name = ''; Src = '手动'; NeedPath = $c.Need }
        }
    }
    if (Read-YesNo "  未在常见目录找到 '$raw', 仍按文件名写入配置 (交给 font2c 递归搜索)?" $false) {
        return [pscustomobject]@{ File = $raw; Path = ''; Name = ''; Src = '手动'; NeedPath = $false }
    }
    return $null
}

# 分页选择字体; 返回所选条目, 用户退出时返回 $null
function Select-FontEntry($all) {
    $filter = ''
    $page = 1
    while ($true) {
        $list = $all
        if ($filter -ne '') {
            $list = @($all | Where-Object { $_.File -like "*$filter*" -or $_.Name -like "*$filter*" })
        }
        $count = @($list).Count
        $pageSize = 15
        try {
            $h = $Host.UI.RawUI.WindowSize.Height
            if ($h -ge 15) { $pageSize = [int][Math]::Min(40, [Math]::Max(8, $h - 9)) }
        } catch { }
        $pages = [int][Math]::Max(1, [Math]::Ceiling($count / $pageSize))
        if ($page -gt $pages) { $page = $pages }
        if ($page -lt 1) { $page = 1 }

        Write-Host ''
        $ftxt = if ($filter -ne '') { "  [筛选: $filter]" } else { '' }
        Write-Host ("--- 字体列表  第 {0}/{1} 页  共 {2} 个{3} ---" -f $page, $pages, $count, $ftxt) -ForegroundColor Cyan
        if ($count -eq 0) {
            Write-Host '  (无匹配结果, 输入 a 清除筛选, 或 m 手动输入)' -ForegroundColor Yellow
        }
        $idxW = ([string][Math]::Max($count, 1)).Length
        $fmt = '  {0,' + $idxW + '}. {1} {2} [{3}]'
        $start = ($page - 1) * $pageSize
        $end = [Math]::Min($start + $pageSize, $count) - 1
        for ($i = $start; $i -le $end; $i++) {
            $e = $list[$i]
            Write-Host ($fmt -f ($i + 1), (Format-Cell $e.File 30), (Format-Cell $e.Name 34), $e.Src)
        }
        Write-Host '  [回车/n]下一页 [p]上一页 [g 页码]跳页 [f 关键字]筛选 [a]全部 [m]手动输入 [q]退出' -ForegroundColor DarkGray
        $cmd = (Read-Raw '  输入编号选择字体, 或输入命令').Trim()

        if ($cmd -eq '' -or $cmd -ieq 'n') { $page++; if ($page -gt $pages) { $page = 1 }; continue }
        if ($cmd -ieq 'p') { $page--; if ($page -lt 1) { $page = $pages }; continue }
        if ($cmd -ieq 'a') { $filter = ''; $page = 1; continue }
        if ($cmd -ieq 'q') { return $null }
        if ($cmd -match '^[Gg]\s*(\d+)$') { $page = [int]$Matches[1]; continue }
        if ($cmd -match '^[Ff]\s+(.+)$') { $filter = $Matches[1].Trim(); $page = 1; continue }
        if ($cmd -ieq 'f') { $filter = (Read-Raw '  输入筛选关键字').Trim(); $page = 1; continue }
        if ($cmd -ieq 'm') {
            $manual = Get-ManualFontEntry
            if ($null -ne $manual) { return $manual }
            continue
        }
        if ($cmd -match '^\d+$') {
            $sel = [int]$cmd
            if ($sel -ge 1 -and $sel -le $count) { return $list[$sel - 1] }
            Write-Host ("  编号超出范围 (1-{0})。" -f $count) -ForegroundColor Yellow
            continue
        }
        Write-Host '  无法识别的输入。' -ForegroundColor Yellow
    }
}

# 解析自定义码点区间: "U+4E00-U+9FA5, 30-39, U+2600" -> 追加到 $rangeList
function ConvertFrom-RangeSpec([string]$spec, $rangeList) {
    foreach ($tokenRaw in ($spec -split ',')) {
        $token = $tokenRaw.Trim()
        if ($token -eq '') { continue }
        if ($token -notmatch '^(?:[Uu]\+)?([0-9A-Fa-f]{1,6})(?:\s*-\s*(?:[Uu]\+)?([0-9A-Fa-f]{1,6}))?$') {
            Write-Host ("  无法解析区间 '{0}', 格式如 U+4E00-U+9FA5 或 30-39。" -f $token) -ForegroundColor Yellow
            return $false
        }
        $s = [Convert]::ToInt32($Matches[1], 16)
        $e = if ($Matches[2]) { [Convert]::ToInt32($Matches[2], 16) } else { $s }
        if ($s -gt $e) {
            Write-Host ("  区间 '{0}' 起点大于终点。" -f $token) -ForegroundColor Yellow
            return $false
        }
        if ($e -gt 0x10FFFF -or ($s -le 0xDFFF -and $e -ge 0xD800)) {
            Write-Host ("  区间 '{0}' 含非法 Unicode 标量 (不能覆盖 U+D800-U+DFFF, 不能超过 U+10FFFF)。" -f $token) -ForegroundColor Yellow
            return $false
        }
        [void]$rangeList.Add(@($s, $e))
    }
    return $true
}

function Read-Charset {
    $presets = @(
        @{ Key = '1'; Desc = 'ASCII 可见字符 U+0020-U+007E (95 字)'; R = @(, @(0x20, 0x7E)) },
        @{ Key = '2'; Desc = '数字 0-9'; R = @(, @(0x30, 0x39)) },
        @{ Key = '3'; Desc = '英文大小写字母 A-Z a-z'; R = @(@(0x41, 0x5A), @(0x61, 0x7A)) },
        @{ Key = '4'; Desc = '常用中文标点/全角符号 (，。！？：；“”·…等)'; R = @(@(0xB7, 0xB7), @(0x2014, 0x2014), @(0x2018, 0x2019), @(0x201C, 0x201D), @(0x2026, 0x2026), @(0x3000, 0x3003), @(0x3005, 0x3005), @(0x3007, 0x3011), @(0x3014, 0x3017), @(0xFF01, 0xFF5E)) },
        @{ Key = '5'; Desc = 'CJK 基本汉字全集 U+4E00-U+9FA5 (20902 字, 体积很大)'; R = @(, @(0x4E00, 0x9FA5)) },
        @{ Key = '6'; Desc = 'Latin-1 补充 U+00A0-U+00FF (西欧重音字符)'; R = @(, @(0xA0, 0xFF)) }
    )
    while ($true) {
        Write-Host ''
        Write-Host '[4/5] 字符集 (决定导出哪些字形)' -ForegroundColor Cyan
        foreach ($p in $presets) { Write-Host ('  {0}. {1}' -f $p.Key, $p.Desc) }
        $ranges = New-Object System.Collections.ArrayList
        $pick = (Read-Raw '  选择预设编号 (可多选, 逗号或空格分隔, 可留空)').Trim()
        $bad = $false
        if ($pick -ne '') {
            foreach ($t in ($pick -split '[,\s]+')) {
                if ($t -eq '') { continue }
                $hit = $presets | Where-Object { $_.Key -eq $t }
                if ($null -eq $hit) {
                    Write-Host ("  没有编号为 '{0}' 的预设。" -f $t) -ForegroundColor Yellow
                    $bad = $true
                    break
                }
                foreach ($r in $hit.R) { [void]$ranges.Add($r) }
            }
        }
        if ($bad) { continue }
        while ($true) {
            $spec = (Read-Raw '  自定义码点区间 (如 U+4E00-U+9FA5, 多个用逗号分隔, 可留空)').Trim()
            if ($spec -eq '') { break }
            if (ConvertFrom-RangeSpec $spec $ranges) { break }
        }
        $chars = Read-Raw '  需要的字符原文 (直接粘贴, 如: 菜单设置返回确定, 可留空)'
        if ($chars -ne '') {
            $seen = New-Object 'System.Collections.Generic.HashSet[string]'
            $sb = New-Object System.Text.StringBuilder
            $i = 0
            while ($i -lt $chars.Length) {
                $len = 1
                if ([char]::IsHighSurrogate($chars[$i]) -and ($i + 1) -lt $chars.Length) { $len = 2 }
                $one = $chars.Substring($i, $len)
                if ($seen.Add($one)) { [void]$sb.Append($one) }
                $i += $len
            }
            $chars = $sb.ToString()
        }
        if ($ranges.Count -eq 0 -and $chars -eq '') {
            Write-Host '  字符集不能为空: 至少选择一个预设、一个区间或输入若干字符。' -ForegroundColor Yellow
            continue
        }
        return @{ Ranges = $ranges; Chars = $chars }
    }
}

# 生成 font2c 规范布局的 JSON 文本 (2 空格缩进, LF, 末尾换行)
function New-ConfigJsonText {
    param($FontFile, $FaceIndex, $Size, $Bpp, $Mode, $Symbol, $Ranges, $Chars, $MissingGlyph)
    $L = New-Object System.Collections.ArrayList
    [void]$L.Add('{')
    [void]$L.Add('  "version": 1,')
    [void]$L.Add(('  "symbol": {0},' -f (ConvertTo-JsonStringLiteral $Symbol)))
    [void]$L.Add('  "font": {')
    if ($FaceIndex -gt 0) {
        [void]$L.Add(('    "file": {0},' -f (ConvertTo-JsonStringLiteral $FontFile)))
        [void]$L.Add(('    "size": {0},' -f $Size))
        [void]$L.Add(('    "face_index": {0}' -f $FaceIndex))
    } else {
        [void]$L.Add(('    "file": {0},' -f (ConvertTo-JsonStringLiteral $FontFile)))
        [void]$L.Add(('    "size": {0}' -f $Size))
    }
    [void]$L.Add('  },')
    [void]$L.Add('  "render": {')
    if ($MissingGlyph -eq 'box') {
        [void]$L.Add(('    "bpp": {0},' -f $Bpp))
        [void]$L.Add('    "missing_glyph": "box"')
    } else {
        [void]$L.Add(('    "bpp": {0}' -f $Bpp))
    }
    [void]$L.Add('  },')
    [void]$L.Add('  "charset": {')
    $hasRanges = ($null -ne $Ranges -and $Ranges.Count -gt 0)
    $hasChars = ($null -ne $Chars -and $Chars -ne '')
    if ($hasRanges) {
        [void]$L.Add('    "ranges": [')
        for ($i = 0; $i -lt $Ranges.Count; $i++) {
            $pair = $Ranges[$i]
            $line = ('      ["{0}", "{1}"]' -f (Format-UToken $pair[0]), (Format-UToken $pair[1]))
            if ($i -lt $Ranges.Count - 1) { $line += ',' }
            [void]$L.Add($line)
        }
        $line = '    ]'
        if ($hasChars) { $line += ',' }
        [void]$L.Add($line)
    }
    if ($hasChars) {
        [void]$L.Add(('    "chars": {0}' -f (ConvertTo-JsonStringLiteral $Chars)))
    }
    [void]$L.Add('  },')
    [void]$L.Add('  "deploy": {')
    [void]$L.Add(('    "mode": {0}' -f (ConvertTo-JsonStringLiteral $Mode)))
    [void]$L.Add('  }')
    [void]$L.Add('}')
    return ($L -join "`n") + "`n"
}

# ---- 字形覆盖预检 (GDI GetGlyphIndices, 仅检查 BMP 码点) ----
$script:GlyphCheckReady = $null

function Initialize-GlyphCheck {
    if ($null -ne $script:GlyphCheckReady) { return $script:GlyphCheckReady }
    $script:GlyphCheckReady = $false
    try {
        if (-not ('F2C.GlyphCheck' -as [type])) {
            Add-Type -ReferencedAssemblies 'System.Drawing' -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace F2C
{
    public static class GlyphCheck
    {
        [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr hdc);
        [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr hdc);
        [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr h);
        [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr h);
        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)]
        static extern uint GetGlyphIndicesW(IntPtr hdc, string s, int c, ushort[] gi, uint fl);
        const uint GGI_MARK_NONEXISTING_GLYPHS = 1;

        // text 中每个 UTF-16 字符是否有字形 (文件内任一字面命中即算有)
        public static bool[] Check(string fontFile, string text)
        {
            var result = new bool[text.Length];
            var pfc = new System.Drawing.Text.PrivateFontCollection();
            try
            {
                pfc.AddFontFile(fontFile);
                var styles = new[] {
                    System.Drawing.FontStyle.Regular, System.Drawing.FontStyle.Bold,
                    System.Drawing.FontStyle.Italic,
                    System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Italic
                };
                foreach (var fam in pfc.Families)
                {
                    System.Drawing.Font font = null;
                    foreach (var st in styles)
                    {
                        if (!fam.IsStyleAvailable(st)) continue;
                        try { font = new System.Drawing.Font(fam, 32f, st, System.Drawing.GraphicsUnit.Pixel); break; }
                        catch { }
                    }
                    if (font == null) continue;
                    IntPtr hfont = IntPtr.Zero, hdc = IntPtr.Zero, old = IntPtr.Zero;
                    try
                    {
                        hfont = font.ToHfont();
                        hdc = CreateCompatibleDC(IntPtr.Zero);
                        old = SelectObject(hdc, hfont);
                        int pos = 0;
                        while (pos < text.Length)
                        {
                            int n = Math.Min(8192, text.Length - pos);
                            var gi = new ushort[n];
                            GetGlyphIndicesW(hdc, text.Substring(pos, n), n, gi, GGI_MARK_NONEXISTING_GLYPHS);
                            for (int i = 0; i < n; i++) { if (gi[i] != 0xFFFF) result[pos + i] = true; }
                            pos += n;
                        }
                    }
                    finally
                    {
                        if (old != IntPtr.Zero) SelectObject(hdc, old);
                        if (hdc != IntPtr.Zero) DeleteDC(hdc);
                        if (hfont != IntPtr.Zero) DeleteObject(hfont);
                        font.Dispose();
                    }
                }
            }
            finally { pfc.Dispose(); }
            return result;
        }
    }
}
'@
        }
        $script:GlyphCheckReady = $true
    } catch { $script:GlyphCheckReady = $false }
    return $script:GlyphCheckReady
}

# 展开字符集并预检字体覆盖; 返回缺失码点列表(升序), 预检不可用时返回 $null
function Get-MissingCodepoints($fontPath, $ranges, $chars) {
    if (-not $fontPath -or -not (Test-Path -LiteralPath $fontPath -PathType Leaf)) { return $null }
    if (-not (Initialize-GlyphCheck)) { return $null }
    $set = New-Object 'System.Collections.Generic.SortedSet[int]'
    if ($null -ne $ranges) {
        foreach ($r in $ranges) {
            for ($c = [int]$r[0]; $c -le [int]$r[1]; $c++) {
                if ($c -le 0xFFFF -and ($c -lt 0xD800 -or $c -gt 0xDFFF)) { [void]$set.Add($c) }
            }
        }
    }
    if ($chars) {
        foreach ($ch in $chars.ToCharArray()) {
            $c = [int]$ch
            if ($c -lt 0xD800 -or $c -gt 0xDFFF) { [void]$set.Add($c) }
        }
    }
    $missing = New-Object System.Collections.ArrayList
    if ($set.Count -eq 0) { return , $missing }
    $sb = New-Object System.Text.StringBuilder
    foreach ($c in $set) { [void]$sb.Append([char]$c) }
    try { $present = [F2C.GlyphCheck]::Check($fontPath, $sb.ToString()) } catch { return $null }
    $i = 0
    foreach ($c in $set) {
        if (-not $present[$i]) { [void]$missing.Add($c) }
        $i++
    }
    return , $missing
}

function Show-MissingSample($missing) {
    $mc = @($missing).Count
    $sample = @($missing) | Select-Object -First 10
    $line = '   '
    foreach ($m in $sample) {
        $disp = if ([int]$m -ge 0x20) { [string][char][int]$m } else { '?' }
        $line += (' {0}({1})' -f (Format-UToken ([int]$m)), $disp)
    }
    Write-Host $line -ForegroundColor Yellow
    if ($mc -gt 10) { Write-Host ('    ... 其余 {0} 个从略' -f ($mc - 10)) -ForegroundColor Yellow }
}

# 查找 font2c.exe: 项目根目录 -> windows\ 子目录
function Get-Font2cExe {
    foreach ($p in @((Join-Path $script:Root 'font2c.exe'), (Join-Path (Join-Path $script:Root 'windows') 'font2c.exe'))) {
        if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    }
    return $null
}

function New-ConfigFlow {
    Write-Host ''
    Write-Host '=== 新建配置 (逐步回答, 大部分问题直接回车用默认值) ===' -ForegroundColor Green
    Write-Host ''
    Write-Host '[1/5] 选择字体文件 (正在扫描字体目录与注册表...)' -ForegroundColor Cyan
    $entries = Get-FontEntries
    if (@($entries).Count -eq 0) {
        Write-Host '  未扫描到任何字体, 请用 m 手动输入路径。' -ForegroundColor Yellow
    }
    $font = Select-FontEntry $entries
    if ($null -eq $font) { Write-Host '已取消新建, 返回列表。'; return }
    $srcInfo = if ($font.Path) { $font.Path } else { '(按文件名交给 font2c 解析)' }
    Write-Host ('  已选择: {0}  {1}' -f $font.File, $srcInfo) -ForegroundColor Green

    # font2c 只搜索 JSON 同目录 / 项目 fonts\ / Windows 字体目录, 用户级字体要特殊处理
    $fontField = $font.File
    if ($font.NeedPath) {
        Write-Host '  注意: 该字体在用户目录, 不在 font2c 的默认搜索范围内。' -ForegroundColor Yellow
        Write-Host '    1. 在 JSON 中写入绝对路径'
        Write-Host '    2. 复制到项目 fonts\ 目录, JSON 只写文件名 (推荐, 可移植)'
        $c = Read-Def '  选择处理方式' '2'
        if ($c -eq '2') {
            $fdir = Join-Path $script:Root 'fonts'
            if (-not (Test-Path -LiteralPath $fdir)) { [void](New-Item -ItemType Directory -Path $fdir) }
            Copy-Item -LiteralPath $font.Path -Destination (Join-Path $fdir $font.File) -Force
            Write-Host ('  已复制到 {0}' -f (Join-Path $fdir $font.File)) -ForegroundColor Green
        } else {
            $fontField = $font.Path -replace '\\', '/'
        }
    }

    $faceIndex = 0
    if ($font.File -match '\.ttc$') {
        while ($true) {
            $v = Read-Def '  该文件是 TTC 字体集合, face_index (0=第一个字面, 通常即可)' '0'
            if ($v -match '^\d+$') { $faceIndex = [int]$v; break }
            Write-Host '  请输入非负整数。' -ForegroundColor Yellow
        }
    }

    Write-Host ''
    Write-Host '[2/5] 渲染参数' -ForegroundColor Cyan
    $size = 16
    while ($true) {
        $v = Read-Def '  字号 (像素高度, 1-4096)' '16'
        if ($v -match '^\d+$' -and [int]$v -ge 1 -and [int]$v -le 4096) { $size = [int]$v; break }
        Write-Host '  字号必须是 1-4096 之间的整数。' -ForegroundColor Yellow
    }
    $bpp = 2
    while ($true) {
        $v = Read-Def '  灰度位深 bpp (1=黑白 2=4级灰 4=16级灰 8=256级灰)' '2'
        if ($v -in '1', '2', '4', '8') { $bpp = [int]$v; break }
        Write-Host '  bpp 只能是 1 / 2 / 4 / 8。' -ForegroundColor Yellow
    }

    Write-Host ''
    Write-Host '[3/5] 部署模式' -ForegroundColor Cyan
    Write-Host '  1. internal - 字模编译进固件 (.c/.h 内嵌位图数组)'
    Write-Host '  2. external - 字模放外部存储 (.c/.h 元数据 + .bin 数据块)'
    $mode = 'internal'
    while ($true) {
        $v = Read-Def '  选择模式' '1'
        if ($v -in '1', 'internal') { $mode = 'internal'; break }
        if ($v -in '2', 'external') { $mode = 'external'; break }
        Write-Host '  请输入 1 或 2。' -ForegroundColor Yellow
    }

    $cs = Read-Charset

    # 配置始终启用 missing_glyph=box; 预检仅提前展示会被边框占位的缺字
    $missingGlyphMode = 'box'
    if ($font.Path) {
        $missing = Get-MissingCodepoints $font.Path $cs.Ranges $cs.Chars
        if ($null -eq $missing) {
            Write-Host '  (字形覆盖预检不可用; 已启用边框占位, 缺字会导出为空心方框并在构建后提示)' -ForegroundColor DarkGray
        } elseif (@($missing).Count -gt 0) {
            $mc = @($missing).Count
            Write-Host ''
            Write-Host ('  提示: 该字体缺少所选字符集中的 {0} 个字形, 将以空心边框占位导出:' -f $mc) -ForegroundColor Yellow
            Show-MissingSample $missing
            Write-Host '  (构建结束后 font2c 会再次列出明细; 若边框过多请考虑换覆盖更全的字体)' -ForegroundColor DarkGray
        }
    }

    Write-Host ''
    Write-Host '[5/5] 输出' -ForegroundColor Cyan
    $base = [IO.Path]::GetFileNameWithoutExtension($font.File).ToLowerInvariant()
    $base = ($base -replace '[^a-z0-9_]', '_') -replace '_+', '_'
    $base = $base.Trim('_')
    if ($base -eq '') { $base = 'font' }
    if ($base -match '^\d') { $base = 'f' + $base }
    $defSymbol = '{0}_{1}_{2}bpp' -f $base, $size, $bpp
    $symbol = $defSymbol
    while ($true) {
        $v = Read-Def '  C 符号前缀 symbol' $defSymbol
        if ($v -match '^[A-Za-z_][A-Za-z0-9_]*$') { $symbol = $v; break }
        Write-Host '  symbol 必须是合法 C 标识符 (字母/数字/下划线, 不能以数字开头)。' -ForegroundColor Yellow
    }

    $jsonPath = ''
    while ($true) {
        $v = Read-Def '  JSON 文件名 (默认存到 input\, 也可输入完整路径)' ($symbol + '.json')
        if (-not $v.ToLower().EndsWith('.json')) { $v = $v + '.json' }
        if ($v -match '^[A-Za-z]:' -or $v.StartsWith('\\')) { $jsonPath = $v }
        elseif ($v -match '[\\/]') { $jsonPath = Join-Path $script:Root $v }
        else { $jsonPath = Join-Path (Join-Path $script:Root 'input') $v }
        if (Test-Path -LiteralPath $jsonPath) {
            if (-not (Read-YesNo ('  {0} 已存在, 覆盖吗?' -f $jsonPath) $false)) { continue }
        }
        break
    }

    $text = New-ConfigJsonText -FontFile $fontField -FaceIndex $faceIndex -Size $size -Bpp $bpp `
        -Mode $mode -Symbol $symbol -Ranges $cs.Ranges -Chars $cs.Chars -MissingGlyph $missingGlyphMode

    Write-Host ''
    Write-Host '生成的配置预览:' -ForegroundColor Cyan
    Write-Host '----------------------------------------'
    Write-Host $text
    Write-Host '----------------------------------------'
    Write-Host ('将写入: {0}  (UTF-8 无 BOM, LF 行尾)' -f $jsonPath)
    if (-not (Read-YesNo '确认写入?' $true)) { Write-Host '已取消, 返回列表。'; return }

    $dir = [IO.Path]::GetDirectoryName($jsonPath)
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Force -Path $dir) }
    [IO.File]::WriteAllText($jsonPath, $text, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host '已写入。' -ForegroundColor Green

    $exe = Get-Font2cExe
    if ($exe) {
        if (Read-YesNo '检测到 font2c.exe, 立即构建试试?' $true) {
            Push-Location $script:Root
            try { & $exe build $jsonPath -o (Join-Path $script:Root 'output') } finally { Pop-Location }
            if ($LASTEXITCODE -eq 0) {
                Write-Host ('构建成功, 产物在 {0}\output' -f $script:Root) -ForegroundColor Green
            } else {
                Write-Host ('构建失败 (退出码 {0}), 请检查上方错误信息。' -f $LASTEXITCODE) -ForegroundColor Red
            }
        }
    } else {
        Write-Host ('提示: 未找到 font2c.exe (根目录或 windows\), 之后可手动运行: font2c build "{0}" -o output' -f $jsonPath) -ForegroundColor DarkGray
    }
    Write-Host ('提示: 可用  font2c scan --src <源码目录> --json "{0}"  把源码里的非 ASCII 字符自动并入 charset.chars' -f $jsonPath) -ForegroundColor DarkGray
}

# 读取 input\*.json 并解析每个配置的摘要
function Get-ConfigSummaries {
    $inputDir = Join-Path $script:Root 'input'
    $items = New-Object System.Collections.ArrayList
    if (-not (Test-Path -LiteralPath $inputDir)) { return , $items }
    $files = Get-ChildItem -LiteralPath $inputDir -File -Filter '*.json' -ErrorAction SilentlyContinue | Sort-Object Name
    foreach ($f in $files) {
        $o = [pscustomobject]@{ File = $f.Name; Path = $f.FullName; Symbol = ''; Font = ''; Size = ''; Bpp = ''; Mode = ''; Ranges = 0; Chars = 0; Box = $false; Ok = $false }
        try {
            $cfg = Get-Content -LiteralPath $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            $o.Symbol = [string]$cfg.symbol
            $o.Font = [string]$cfg.font.file
            $o.Size = [string]$cfg.font.size
            $o.Bpp = [string]$cfg.render.bpp
            $o.Mode = [string]$cfg.deploy.mode
            if ($null -ne $cfg.render.missing_glyph -and [string]$cfg.render.missing_glyph -eq 'box') { $o.Box = $true }
            if ($null -ne $cfg.charset.ranges) { $o.Ranges = @($cfg.charset.ranges).Count }
            if ($null -ne $cfg.charset.chars) { $o.Chars = ([string]$cfg.charset.chars).Length }
            $o.Ok = $true
        } catch { }
        [void]$items.Add($o)
    }
    return , $items
}

function Show-ConfigContent($item) {
    Write-Host ''
    Write-Host ('--- {0} ---' -f $item.Path) -ForegroundColor Cyan
    try {
        Get-Content -LiteralPath $item.Path -Raw -Encoding UTF8 | Write-Host
    } catch {
        Write-Host ('读取失败: {0}' -f $_.Exception.Message) -ForegroundColor Red
    }
}

# 对已有配置做字形预检, 缺失时可剔除并按规范布局重写
function Repair-ConfigGlyphs($item) {
    try {
        $cfg = Get-Content -LiteralPath $item.Path -Raw -Encoding UTF8 | ConvertFrom-Json
        $ranges = New-Object System.Collections.ArrayList
        if ($null -ne $cfg.charset.ranges) {
            foreach ($pair in $cfg.charset.ranges) {
                $a = [Convert]::ToInt32((([string]$pair[0]) -replace '^[Uu]\+', ''), 16)
                $b = [Convert]::ToInt32((([string]$pair[1]) -replace '^[Uu]\+', ''), 16)
                [void]$ranges.Add(@($a, $b))
            }
        }
        $chars = ''
        if ($null -ne $cfg.charset.chars) { $chars = [string]$cfg.charset.chars }
        $ff = [string]$cfg.font.file
    } catch {
        Write-Host ('  解析配置失败: {0}' -f $_.Exception.Message) -ForegroundColor Red
        return
    }
    # 按 font2c 的顺序找字体文件: JSON 同目录 -> 项目 fonts\ -> 系统/用户字体目录
    $cand = New-Object System.Collections.ArrayList
    [void]$cand.Add((Join-Path (Split-Path -Parent $item.Path) $ff))
    [void]$cand.Add((Join-Path (Join-Path $script:Root 'fonts') $ff))
    $baseName = [IO.Path]::GetFileName($ff)
    if ($env:WINDIR) { [void]$cand.Add((Join-Path (Join-Path $env:WINDIR 'Fonts') $baseName)) }
    if ($env:LOCALAPPDATA) { [void]$cand.Add((Join-Path (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts') $baseName)) }
    $fontPath = $null
    foreach ($p in $cand) { if (Test-Path -LiteralPath $p -PathType Leaf) { $fontPath = $p; break } }
    if (-not $fontPath) {
        Write-Host ('  找不到字体文件 {0}, 无法预检。' -f $ff) -ForegroundColor Yellow
        return
    }
    $missing = Get-MissingCodepoints $fontPath $ranges $chars
    if ($null -eq $missing) { Write-Host '  字形覆盖预检不可用。' -ForegroundColor Yellow; return }
    $hasBox = ($null -ne $cfg.render.missing_glyph -and [string]$cfg.render.missing_glyph -eq 'box')
    if (@($missing).Count -eq 0) {
        Write-Host ('  检查通过: {0} 覆盖该配置的全部字形。' -f $ff) -ForegroundColor Green
        return
    }
    $mc = @($missing).Count
    if ($hasBox) {
        Write-Host ('  该配置已启用边框占位: {0} 个缺字构建时会导出为空心方框:' -f $mc) -ForegroundColor Green
        Show-MissingSample $missing
        return
    }
    Write-Host ('  该字体缺少配置中的 {0} 个字形:' -f $mc) -ForegroundColor Yellow
    Show-MissingSample $missing
    if (-not (Read-YesNo '  启用边框占位 (render.missing_glyph=box) 并重写配置?' $true)) { return }
    $faceIndex = 0
    if ($null -ne $cfg.font.face_index) { $faceIndex = [int]$cfg.font.face_index }
    try {
        $text = New-ConfigJsonText -FontFile $ff -FaceIndex $faceIndex -Size ([int]$cfg.font.size) -Bpp ([int]$cfg.render.bpp) `
            -Mode ([string]$cfg.deploy.mode) -Symbol ([string]$cfg.symbol) -Ranges $ranges -Chars $chars -MissingGlyph 'box'
        [IO.File]::WriteAllText($item.Path, $text, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host ('  已重写 {0}: 启用边框占位, {1} 个缺字构建时将输出为空心方框。' -f $item.File, $mc) -ForegroundColor Green
    } catch {
        Write-Host ('  重写失败: {0}' -f $_.Exception.Message) -ForegroundColor Red
    }
}

# 主菜单: 已有配置列表 + 指令
function Invoke-Wizard {
    Write-Host ''
    Write-Host '================ font2c 配置向导 ================' -ForegroundColor Green
    Write-Host ('  项目目录: {0}' -f $script:Root)
    $page = 1
    while ($true) {
        $cfgs = Get-ConfigSummaries
        $count = @($cfgs).Count
        $pageSize = 15
        try {
            $h = $Host.UI.RawUI.WindowSize.Height
            if ($h -ge 15) { $pageSize = [int][Math]::Min(40, [Math]::Max(8, $h - 9)) }
        } catch { }
        $pages = [int][Math]::Max(1, [Math]::Ceiling($count / $pageSize))
        if ($page -gt $pages) { $page = $pages }
        if ($page -lt 1) { $page = 1 }

        Write-Host ''
        Write-Host ('--- 已有配置 (input\*.json)  第 {0}/{1} 页  共 {2} 个 ---' -f $page, $pages, $count) -ForegroundColor Cyan
        if ($count -eq 0) {
            Write-Host '  (还没有配置文件, 输入 a 新建一个)' -ForegroundColor Yellow
        } else {
            $idxW = ([string]$count).Length
            $fmt = '  {0,' + $idxW + '}. {1} {2} {3,6} {4,4} {5} {6}'
            $start = ($page - 1) * $pageSize
            $end = [Math]::Min($start + $pageSize, $count) - 1
            for ($i = $start; $i -le $end; $i++) {
                $c = $cfgs[$i]
                if ($c.Ok) {
                    $csDesc = ''
                    if ($c.Ranges -gt 0) { $csDesc += ('{0}区间' -f $c.Ranges) }
                    if ($c.Chars -gt 0) { if ($csDesc -ne '') { $csDesc += '+' }; $csDesc += ('{0}字' -f $c.Chars) }
                    if ($c.Box) { $csDesc += ' 缺字□' }
                    Write-Host ($fmt -f ($i + 1), (Format-Cell $c.File 34), (Format-Cell $c.Font 14), ($c.Size + 'px'), ($c.Bpp + 'bpp'), (Format-Cell $c.Mode 8), $csDesc)
                } else {
                    Write-Host ($fmt -f ($i + 1), (Format-Cell $c.File 34), (Format-Cell '(解析失败)' 14), '', '', '', '')
                }
            }
        }
        Write-Host '  [a]新建配置  [编号]查看内容  [b 编号]构建  [c 编号]检查缺字  [回车/n]下页  [p]上页  [q]退出' -ForegroundColor DarkGray
        $cmd = (Read-Raw '  输入指令').Trim()

        if ($cmd -ieq 'a') { New-ConfigFlow; continue }
        if ($cmd -ieq 'q') { break }
        if ($cmd -eq '' -or $cmd -ieq 'n') { $page++; if ($page -gt $pages) { $page = 1 }; continue }
        if ($cmd -ieq 'p') { $page--; if ($page -lt 1) { $page = $pages }; continue }
        if ($cmd -match '^[Bb]\s*(\d+)$') {
            $sel = [int]$Matches[1]
            if ($sel -lt 1 -or $sel -gt $count) {
                Write-Host ('  编号超出范围 (1-{0})。' -f $count) -ForegroundColor Yellow
                continue
            }
            $exe = Get-Font2cExe
            if (-not $exe) {
                Write-Host '  未找到 font2c.exe (根目录或 windows\), 无法构建。' -ForegroundColor Yellow
                continue
            }
            $item = $cfgs[$sel - 1]
            Write-Host ('  构建 {0} ...' -f $item.File)
            Push-Location $script:Root
            try { & $exe build $item.Path -o (Join-Path $script:Root 'output') } finally { Pop-Location }
            if ($LASTEXITCODE -eq 0) {
                Write-Host ('  构建成功, 产物在 {0}\output' -f $script:Root) -ForegroundColor Green
            } else {
                Write-Host ('  构建失败 (退出码 {0}), 请检查上方错误信息。' -f $LASTEXITCODE) -ForegroundColor Red
            }
            continue
        }
        if ($cmd -match '^[Cc]\s*(\d+)$') {
            $sel = [int]$Matches[1]
            if ($sel -ge 1 -and $sel -le $count) { Repair-ConfigGlyphs $cfgs[$sel - 1] }
            else { Write-Host ('  编号超出范围 (1-{0})。' -f $count) -ForegroundColor Yellow }
            continue
        }
        if ($cmd -match '^\d+$') {
            $sel = [int]$cmd
            if ($sel -ge 1 -and $sel -le $count) { Show-ConfigContent $cfgs[$sel - 1] }
            else { Write-Host ('  编号超出范围 (1-{0})。' -f $count) -ForegroundColor Yellow }
            continue
        }
        Write-Host '  无法识别的指令。' -ForegroundColor Yellow
    }
    try { Read-Host '按回车键退出' | Out-Null } catch { }
}

if ($env:F2C_WIZ_NOMAIN -ne '1') {
    try {
        Invoke-Wizard
    } catch {
        Write-Host ''
        Write-Host ('向导执行出错: ' + $_.Exception.Message) -ForegroundColor Red
        if ($script:EmptyStreak -lt 100) { try { Read-Host '按回车键退出' | Out-Null } catch { } }
        exit 1
    }
}
