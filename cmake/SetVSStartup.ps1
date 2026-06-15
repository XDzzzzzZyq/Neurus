# Patches a Visual Studio .sln file to set a specific project as the default startup.
# Usage: cmake\SetVSStartup.ps1 <slnPath> <projectName>
param([string]$SlnPath, [string]$ProjectName = "Neurus")

$sln = Get-Content $SlnPath -Raw
if (-not $sln) { Write-Error "Cannot read $SlnPath"; exit 1 }

# Extract project GUID
$match = [regex]::Match($sln, "Project\(.+?\)\s*=\s*`"$ProjectName`",\s*`"[^`"]+`",\s*`"\{([A-F0-9-]+)\}`"")
if (-not $match.Success) { Write-Error "Project '$ProjectName' not found in solution"; exit 1 }

$guid = $match.Groups[1].Value

# Insert GlobalSection(SolutionProperties) before GlobalSection(NestedProjects)
$section = "`tGlobalSection(SolutionProperties) = preSolution`r`n`t`tHideSolutionNode = FALSE`r`n`t`tStartupProject = {$guid}`r`n`tEndGlobalSection`r`n"

$sln = $sln -replace "(?=`tGlobalSection\(NestedProjects\))", $section

Set-Content $SlnPath -Value $sln -NoNewline
Write-Output "  Set '$ProjectName' ({$guid}) as VS startup project"
