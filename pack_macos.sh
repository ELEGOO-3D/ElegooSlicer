#!/bin/bash

echo "Packing ElegooSlicer for MacOS..."

# 加载 .env 文件
if [ -f .env ]; then
  source .env
else
  echo ".env file not found!"
  exit 1
fi

# 初始化变量
ARCH=""
BUILD_DEPENDENCIES=false
BUILD_FLAG=""

# 解析命令行参数
while getopts "a:de" opt; do
  case $opt in
    a)
      ARCH=$OPTARG
      ;;
    d)
      BUILD_DEPENDENCIES=true
      ;;
    e)
      BUILD_FLAG="-e"
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      exit 1
      ;;
    :)
      echo "Option -$OPTARG requires an argument." >&2
      exit 1
      ;;
  esac
done

# 检查是否提供了必要的参数
if [ -z "$ARCH" ]; then
  echo "Usage: $0 -a <arch> (arm64 or x86_64) [-d]"
  exit 1
fi

# 获取当前路径
CURRENT_DIR=$(pwd)
echo "Current directory: $CURRENT_DIR"
APP_NAME="ElegooSlicer"

# 删除文件夹"${CURRENT_DIR}/build/${ARCH}/ElegooSlicer" 
rm -rf "${CURRENT_DIR}/build/${ARCH}/ElegooSlicer"

# 构建依赖项
if [ "$BUILD_DEPENDENCIES" = true ]; then
  ./build_release_macos.sh -dx -a ${ARCH} -t 10.15 -1 ${BUILD_FLAG} -w

  # 判断是否构建成功
  if [ $? -ne 0 ]; then
    echo "Failed to build dependencies!"
    exit 1
  fi

fi


# 构建 ElegooSlicer
./build_release_macos.sh -s -n -x -a ${ARCH}  -t 10.15 -1 ${BUILD_FLAG} -g

# 判断是否构建成功
if [ $? -ne 0 ]; then
  echo "Failed to build ElegooSlicer!"
  exit 1
fi


# 读取Info.plist中的CFBundleShortVersionString值
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "${CURRENT_DIR}/build/${ARCH}/ElegooSlicer/ElegooSlicer.app/Contents/Info.plist")
echo "Current version: ${VERSION}"
INSTALLER_NAME="ElegooSlicer_Mac_${ARCH}_V${VERSION}.dmg"

# 删除ElegooSlicer_profile_validator.app
echo "Removing ElegooSlicer_profile_validator.app..."
rm -rf "${CURRENT_DIR}/build/${ARCH}/ElegooSlicer/ElegooSlicer_profile_validator.app"

# 代码签名
codesign --deep --force --verbose --options runtime --timestamp --entitlements "$CURRENT_DIR/scripts/disable_validation.entitlements" --sign "${CERTIFICATE_ID}" "$CURRENT_DIR/build/${ARCH}/ElegooSlicer/ElegooSlicer.app"

# 创建符号链接
ln -s /Applications "${CURRENT_DIR}/build/${ARCH}/ElegooSlicer/Applications"
# 创建安装包
hdiutil create -volname "ElegooSlicer" -srcfolder "${CURRENT_DIR}/build/${ARCH}/ElegooSlicer" -ov -format UDZO "${INSTALLER_NAME}"
# 代码签名
codesign --deep --force --verbose --options runtime --timestamp --entitlements "${CURRENT_DIR}/scripts/disable_validation.entitlements" --sign "${CERTIFICATE_ID}" "${INSTALLER_NAME}"
# 验证签名
xcrun notarytool store-credentials "notarytool-profile" --apple-id "${APPLE_ID}" --team-id "${MAC_TEAM_ID}" --password "${MAC_APP_PWD}"

xcrun notarytool submit "${INSTALLER_NAME}" --keychain-profile "notarytool-profile" --wait

xcrun stapler staple "${INSTALLER_NAME}"
