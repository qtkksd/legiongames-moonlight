pipeline {
  agent none
  options { timestamps(); disableConcurrentBuilds() }
  triggers { pollSCM('H/2 * * * *') }
  stages {
    stage('Prepare') {
      agent { label 'controller' }
      steps {
        checkout([$class: 'GitSCM', userRemoteConfigs: [[url: 'https://github.com/qtkksd/legiongames-moonlight.git', credentialsId: 'github-qtkksd']], branches: [[name: '*/legiongames-moonlight']]])
        script {
          env.GIT_SHA = sh(script: 'git rev-parse --short=7 HEAD', returnStdout: true).trim()
          env.GIT_SHA_FULL = sh(script: 'git rev-parse HEAD', returnStdout: true).trim()
        }
      }
    }
    stage('Windows (MSVC2022 + Qt6 x64)') {
      agent { label 'windows-msys2' }
      steps {
        checkout([$class: 'GitSCM', userRemoteConfigs: [[url: 'https://github.com/qtkksd/legiongames-moonlight.git', credentialsId: 'github-qtkksd']], branches: [[name: '*/legiongames-moonlight']]])
        writeFile file: 'ci-patch.ps1', text: '''$p='globaldefs.pri'
(Get-Content $p) -replace [regex]::Escape('QMAKE_LFLAGS += -cetcompat'),'# ci' | Set-Content $p -Encoding ascii
$p='scripts/generate-bundle.bat'
(Get-Content $p) -replace [regex]::Escape('if not exist "%BUILD_ROOT%\\build-arm64-%BUILD_CONFIG%\\Moonlight.msi" ('),'if 1==2 (' | Set-Content $p -Encoding ascii
$p='wix/MoonlightSetup/Bundle.wxs'
(Get-Content $p -Raw) -replace '(?s)\\s*<MsiPackage Id="Moonlight_arm64".*?</MsiPackage>','' | Set-Content $p -Encoding ascii
'''
        writeFile file: 'ci-build-win.bat', text: '''@echo off
setlocal enableextensions
powershell -NoProfile -ExecutionPolicy Bypass -File ci-patch.ps1
set "VSPATH=C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools"
set "WIX=C:\\wix"
set "DOTNET_ROOT=C:\\dotnet"
set "PATH=C:\\Program Files\\7-Zip;C:\\wix;C:\\dotnet;C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\MSBuild\\Current\\Bin;%PATH%"
set "CI_VERSION=%GIT_SHA%"
set "COMMIT=%GIT_SHA_FULL%"
set "TAG=" & set "EXACT=" & set "DESC="
for /f "delims=" %%i in ('git describe --tags --abbrev=0 2^>nul') do set "TAG=%%i"
for /f "delims=" %%i in ('git describe --tags --exact-match 2^>nul') do set "EXACT=%%i"
for /f "delims=" %%i in ('git describe --tags --always 2^>nul') do set "DESC=%%i"
> ci-meta.properties echo TAG=%TAG%
>> ci-meta.properties echo EXACT=%EXACT%
>> ci-meta.properties echo DESC=%DESC%
>> ci-meta.properties echo COMMIT=%COMMIT%
>> ci-meta.properties echo SHORT=%GIT_SHA%

rem ===== x64 =====
set "PATH=C:\\Qt\\6.11.0\\msvc2022_64\\bin;%PATH%"
call "%VSPATH%\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64
for /f "usebackq delims=" %%i in (`"%CD%\\scripts\\vswhere.exe" -latest -products * -find VC\\Redist\\MSVC\\*\\x64\\Microsoft.VC*.CRT`) do set "VC_REDIST_DLL_PATH=%%i"
call scripts\\build-arch.bat Release x64
if errorlevel 1 exit /b 1

rem ===== bundle (x64 only) =====
call scripts\\generate-bundle.bat Release
if errorlevel 1 exit /b 1

if not exist artifacts\\windows mkdir artifacts\\windows
for %%f in (build\\installer-Release\\MoonlightSetup-*.exe) do copy /y "%%f" "artifacts\\windows\\MoonlightSetup.exe" >nul
powershell -NoProfile -Command "Compress-Archive -Path 'build/deploy-x64-release/*' -DestinationPath 'artifacts/windows/Moonlight-Windows-x64.zip' -Force"
exit /b 0
'''
        bat 'ci-build-win.bat'
        archiveArtifacts artifacts: 'artifacts/windows/**, ci-meta.properties', allowEmptyArchive: true
        stash name: 'win', includes: 'artifacts/windows/**, ci-meta.properties'
      }
    }
    stage('Linux AppImage') {
      agent { label 'controller' }
      steps {
        checkout([$class: 'GitSCM', userRemoteConfigs: [[url: 'https://github.com/qtkksd/legiongames-moonlight.git', credentialsId: 'github-qtkksd']], branches: [[name: '*/legiongames-moonlight']]])
        sh '''#!/bin/bash
set -euo pipefail
HOSTWS=$(printf '%s' "$WORKSPACE" | sed 's#/var/jenkins_home#/root/jenkins/home#')
docker run --rm -v "$HOSTWS":/src -w /src -e CI_VERSION="$GIT_SHA" -e APPIMAGE_EXTRACT_AND_RUN=1 moonlight-build:22.04 bash -lc 'scripts/build-appimage.sh'
mkdir -p artifacts/linux
cp build/installer-release/Moonlight-*-x86_64.AppImage artifacts/linux/Moonlight-x86_64.AppImage
ls -la artifacts/linux
'''
        archiveArtifacts artifacts: 'artifacts/linux/**', allowEmptyArchive: true
        stash name: 'lin', includes: 'artifacts/linux/**'
      }
    }
    stage('Publish (edge)') {
      agent { label 'controller' }
      steps {
        unstash 'win'
        unstash 'lin'
        script {
          def meta = readProperties file: 'ci-meta.properties'
          def tag   = (meta.TAG ?: '').trim()
          def desc  = (meta.DESC ?: '').trim()
          def sha   = (meta.SHORT ?: env.GIT_SHA ?: '').trim()
          def full  = (meta.COMMIT ?: '').trim()
          if (!sha) { error 'No commit SHA available.' }
          def verNoV = tag.startsWith('v') ? tag.substring(1) : (tag ?: '0.0.0')
          def base = '/pkgs/moonlight'
          def cdir = "${base}/commits/${sha}"
          sh "mkdir -p ${cdir}/windows ${cdir}/linux"
          sh "cp artifacts/windows/MoonlightSetup.exe ${cdir}/windows/ 2>/dev/null || true"
          sh "cp artifacts/windows/Moonlight-Windows-x64.zip ${cdir}/windows/ 2>/dev/null || true"
          sh "cp artifacts/linux/Moonlight-x86_64.AppImage ${cdir}/linux/ 2>/dev/null || true"
          def hW = sh(script: "sha256sum ${cdir}/windows/MoonlightSetup.exe | cut -d' ' -f1", returnStdout: true).trim()
          def sW = sh(script: "stat -c %s ${cdir}/windows/MoonlightSetup.exe", returnStdout: true).trim()
          def hL = sh(script: "sha256sum ${cdir}/linux/Moonlight-x86_64.AppImage | cut -d' ' -f1", returnStdout: true).trim()
          def sL = sh(script: "stat -c %s ${cdir}/linux/Moonlight-x86_64.AppImage", returnStdout: true).trim()
          def ts = sh(script: "date -u +%Y-%m-%dT%H:%M:%SZ", returnStdout: true).trim()
          def manifest = """{
  "version": "${verNoV}",
  "tag": "${tag}",
  "commit": "${sha}",
  "commit_full": "${full}",
  "describe": "${desc}",
  "build": ${env.BUILD_NUMBER},
  "channel": "edge",
  "min_supported": "0.0.0",
  "released_at": "${ts}",
  "assets": {
    "windows": { "url": "https://pkgs.legiongames.ru/moonlight/edge/latest/windows/MoonlightSetup.exe", "sha256": "${hW}", "size": ${sW} },
    "linux":   { "url": "https://pkgs.legiongames.ru/moonlight/edge/latest/linux/Moonlight-x86_64.AppImage", "sha256": "${hL}", "size": ${sL} }
  }
}
"""
          writeFile file: "${cdir}/manifest.json", text: manifest
          writeFile file: "${cdir}/version", text: sha
          sh "mkdir -p ${base}/edge && cd ${base}/edge && ln -sfn ../commits/${sha} latest.new && mv -T latest.new latest"
          echo "Published moonlight edge ${sha}: version=${verNoV} commit=${sha} build=${env.BUILD_NUMBER}"
        }
      }
    }
  }
}
