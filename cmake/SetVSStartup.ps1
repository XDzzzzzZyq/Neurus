# Patches a Visual Studio .sln file to set a specific project as the default startup.
# Usage: powershell -File cmake\SetVSStartup.ps1 -SlnPath <path> [-ProjectName Neurus]
param([string]$SlnPath, [string]$ProjectName = "Neurus")

$lines = @(Get-Content $SlnPath -Encoding UTF8)
if (-not $lines) { Write-Error "Cannot read $SlnPath"; exit 1 }

# Extract project GUID from line like: Project("{...}") = "Neurus", "src\Neurus.vcxproj", "{GUID}"
$guid = $null
foreach ($line in $lines) {
    if ($line -match "Project\(.+?\)\s*=\s*`"$ProjectName`",\s*`"[^`"]+`",\s*`"\{([A-F0-9-]+)\}`"") {
        $guid = $Matches[1]
        break
    }
}
if (-not $guid) { Write-Error "Project '$ProjectName' not found in solution"; exit 1 }

# Find the Global section and insert SolutionProperties before NestedProjects
$out = [System.Collections.ArrayList]::new()
foreach ($line in $lines) {
    if ($line -match "^\tGlobalSection\(NestedProjects\)") {
        # Insert SolutionProperties section before NestedProjects
        [void]$out.Add("`tGlobalSection(SolutionProperties) = preSolution")
        [void]$out.Add("`t`tHideSolutionNode = FALSE")
        [void]$out.Add("`t`tStartupProject = {$guid}")
        [void]$out.Add("`tEndGlobalSection")
    }
    [void]$out.Add($line)
}

[System.IO.File]::WriteAllLines($SlnPath, $out, [System.Text.UTF8Encoding]::new($false))
Write-Output "  Set '$ProjectName' ({$guid}) as VS startup project"
