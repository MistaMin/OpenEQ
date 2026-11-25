; Windows installer. Built by the native-builds workflow: ISCC -DVersion=x.y.z packaging\windows.iss
#define Art "..\build\OpenEQ_artefacts\Release"
[Setup]
AppId=com.opengrid.openeq
AppName=OpenEQ
AppVersion={#Version}
AppPublisher=OpenGrid
AppPublisherURL=https://opengrid.studio
AppCopyright=Copyright (c) 2026 Marcos Quirino Deida
DefaultDirName={commonpf}\OpenEQ
DisableDirPage=yes
LicenseFile=..\BINARY_LICENSE.txt
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=OpenEQ-{#Version}-windows-x64-setup
[Types]
Name: "full"; Description: "All formats"
Name: "custom"; Description: "Choose formats"; Flags: iscustom
[Components]
Name: "vst3"; Description: "VST3"; Types: full custom
Name: "clap"; Description: "CLAP"; Types: full custom
Name: "lv2"; Description: "LV2"; Types: full custom
[Files]
Source: "{#Art}\VST3\OpenEQ.vst3\*"; DestDir: "{commoncf}\VST3\OpenEQ.vst3"; Flags: recursesubdirs ignoreversion; Components: vst3
Source: "{#Art}\CLAP\OpenEQ.clap"; DestDir: "{commoncf}\CLAP"; Flags: ignoreversion; Components: clap
Source: "{#Art}\LV2\OpenEQ.lv2\*"; DestDir: "{commoncf}\LV2\OpenEQ.lv2"; Flags: recursesubdirs ignoreversion; Components: lv2
Source: "..\LICENSE"; DestDir: "{app}"
Source: "..\BINARY_LICENSE.txt"; DestDir: "{app}"
Source: "..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"
Source: "..\README.md"; DestDir: "{app}"
Source: "..\CHANGELOG.md"; DestDir: "{app}"
Source: "..\RELEASE_NOTES.md"; DestDir: "{app}"
