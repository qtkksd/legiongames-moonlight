BUILD_CONFIG="release"

fail()
{
	echo "$1" 1>&2
	exit 1
}

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
DEPLOY_FOLDER=$BUILD_ROOT/deploy-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  VERSION=`cat $SOURCE_ROOT/app/version.txt`
fi

command -v qmake6 >/dev/null 2>&1 || fail "Unable to find 'qmake6' in your PATH!"
command -v linuxdeployqt >/dev/null 2>&1 || fail "Unable to find 'linuxdeployqt' in your PATH!"

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir $BUILD_ROOT
mkdir $BUILD_FOLDER
mkdir $DEPLOY_FOLDER
mkdir $INSTALLER_FOLDER

echo Configuring the project
pushd $BUILD_FOLDER
# Building with Wayland support will cause linuxdeployqt to include libwayland-client.so in the AppImage.
# Since we always use the host implementation of EGL, this can cause libEGL_mesa.so to fail to load due
# to missing symbols from the host's version of libwayland-client.so that aren't present in the older
# version of libwayland-client.so from our AppImage build environment. When this happens, EGL fails to
# work even in X11. To avoid this, we will disable Wayland support for the AppImage.
#
# We disable DRM support because linuxdeployqt doesn't bundle the appropriate libraries for Qt EGLFS.
qmake6 $SOURCE_ROOT/moonlight-qt.pro CONFIG+=disable-wayland CONFIG+=disable-libdrm PREFIX=$DEPLOY_FOLDER/usr DEFINES+=APP_IMAGE || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Deploying to staging directory
pushd $BUILD_FOLDER
make install || fail "Make install failed!"
popd

# Licence and notice files must ship with the binary, not only in the repository.
# Keep the AppImage layout conventional so the files are easy to find offline.
echo Staging licences and notices
mkdir -p $DEPLOY_FOLDER/usr/share/doc/moonlight/LICENSES
cp $SOURCE_ROOT/THIRD-PARTY-NOTICES.md $DEPLOY_FOLDER/usr/share/doc/moonlight/ || fail "Notice copy failed!"
cp $SOURCE_ROOT/LICENSE $DEPLOY_FOLDER/usr/share/doc/moonlight/ || fail "GPLv3 copy failed!"
cp $SOURCE_ROOT/LICENSES/BSD-3-Clause.txt $DEPLOY_FOLDER/usr/share/doc/moonlight/LICENSES/ || fail "Licence copy failed!"

# We need to manually place SDL3 in our AppImage, since linuxdeployqt
# cannot see the dependency via ldd when it looks at SDL2-compat.
echo Staging SDL3 library
mkdir -p $DEPLOY_FOLDER/usr/lib
cp /usr/local/lib/libSDL3.so.0 $DEPLOY_FOLDER/usr/lib/

# The app links OpenSSL directly, so linuxdeployqt would bundle the build
# host's libssl/libcrypto. Qt's TLS plugin then mixes the bundled and system
# copies, which segfaults inside SSL_new/SSL_clear on distributions with a
# different OpenSSL 3 patch level. Keep the host's single OpenSSL copy.
rm -f $DEPLOY_FOLDER/usr/lib/libssl.so.3 $DEPLOY_FOLDER/usr/lib/libcrypto.so.3

echo Creating AppImage
pushd $INSTALLER_FOLDER
VERSION=$VERSION linuxdeployqt $DEPLOY_FOLDER/usr/share/applications/com.moonlight_stream.Moonlight.desktop \
  -qmake=qmake6 -qmldir=$SOURCE_ROOT/app/gui -appimage -extra-plugins=tls \
  -exclude-libs=libssl.so.3,libcrypto.so.3 \
  -executable=$DEPLOY_FOLDER/usr/lib/libSDL3.so.0 || fail "linuxdeployqt failed!"
popd

echo Build successful