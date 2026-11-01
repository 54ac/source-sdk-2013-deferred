[CmdletBinding()]
param (
    [Parameter(Mandatory=$true, ValueFromPipeline=$true)][System.IO.FileInfo]$File,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$false)][switch]$Dynamic,
    [Parameter(Mandatory=$false)][System.UInt32]$Threads
)

if ($Version -notin @("20b", "30", "40", "41", "50", "51")) {
	return
}

$shaders = [System.Collections.Generic.List[string]]::new()
$fileList = $File.OpenText()
while ($null -ne ($line = $fileList.ReadLine())) {
	$trimmed = $line.Trim()
	if ($trimmed -eq '' -or $trimmed.StartsWith('//')) {
		continue
	}
	$shaders.Add($trimmed)
}
$fileList.Close()

if ($shaders.Count -eq 0) {
	return
}

$argsList = [System.Collections.Generic.List[string]]::new()
if ($Dynamic) {
	$argsList.Add("-dynamic")
}
if ($Threads -ne 0) {
	$argsList.Add("-threads")
	$argsList.Add($Threads.ToString())
}
$argsList.Add("-ver")
$argsList.Add($Version)
$argsList.Add("-shaderpath")
$argsList.Add($File.DirectoryName)
$argsList.AddRange($shaders)

& "$PSScriptRoot\ShaderCompile2" @argsList
