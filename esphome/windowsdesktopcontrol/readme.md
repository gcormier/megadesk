Configuration guide

This guide assumes you have at least stored position 1 and 2 on your megadesk (which would be two clicks and three clicks), where the first one reflects the lower (sitting) and the second the upper (standing) position. This approach allows to use windows scripts, so no additional software needs to be installed - just a script and some configuration. There might be smarter solutions, but this one is quite straight forward and very lightweighted, so might want to give it a try. 

1) Download **megadesk/esphome/windowsdesktopcontrol/** and save its contents to **C:\Users\%USERNAME%\Bekant** on your harddisk. 
2) Open **megadesk.ps1** in a text editor and replace [IP OR DNS OF YOUR ESP DEVICE] with the IP address or DNS of your ESP Device. I.e.  $yourESPDevice = "http://bekant.megadesk.com" or $yourESPDevice = "http://192.168.0.1"
3) Create a windows shortcut to **megadesk.ps1** and rename it to "Megadesk down". Open the shortcut's properties. Into the field "target" copy : 
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -noLogo -ExecutionPolicy unrestricted -command  "& ‘C:\Users\%USERNAME%\Bekant\megadesk.ps1’ 2"
Then choose a symbol you like, i.e. megadesk/esphome/windowsdesktopcontrol/down.ico
You could also add a keyboard shortcut to trigger the script via keyboard. 
4) Create a second windows shortcut, same procedure as above but name ut "Megadesk up" and use 
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -noLogo -ExecutionPolicy unrestricted -command  "& ‘C:\Users\%USERNAME%\Bekant\megadesk.ps1’ 3"
5) Drag and Drop both shortcuts over the task bar. 
6) That's it. 

If Windows blocks the execution of the script, this is because of Windows' default security settings. You might have to adjust this settings by opening a powershell console and run: **Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope CurrentUser**
https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.security/set-executionpolicy?view=powershell-7.3
