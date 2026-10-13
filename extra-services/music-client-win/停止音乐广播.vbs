Set shell = CreateObject("WScript.Shell")
Set files = CreateObject("Scripting.FileSystemObject")
folder = files.GetParentFolderName(WScript.ScriptFullName)
shell.Run """" & files.BuildPath(folder, "StillinkMusic.exe") & """ --stop", 0, False
