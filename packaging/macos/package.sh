#!/usr/bin/env bash
# Builds the macOS installer (.dmg) from the built app.
#
#   packaging/macos/package.sh <path/to/Guitar Studio Pro.app> <version> <output dir>
#
# Without signing secrets the app gets an ad-hoc signature (it runs, but macOS asks
# for "Abrir mesmo assim" the first time). With the MACOS_* / APPLE_* variables set
# (see docs/ASSINATURA.md) the app and the .dmg are signed with the Developer ID,
# notarized by Apple and stapled, so it opens with no warning.
set -euo pipefail

APP="$1"; VERSION="$2"; OUT="$3"
NAME="Guitar Studio Pro"
DMG="$OUT/GuitarStudioPro-$VERSION-macOS.dmg"
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"

lipo -info "$APP/Contents/MacOS/$NAME"

if [[ -n "${MACOS_CERT_P12:-}" && -n "${MACOS_SIGN_IDENTITY:-}" ]]; then
  echo "Signing with Developer ID"
  KEYCHAIN="$RUNNER_TEMP/gsp-signing.keychain-db"
  KEYPASS="$(uuidgen)"
  echo "$MACOS_CERT_P12" | base64 --decode > "$RUNNER_TEMP/cert.p12"
  security create-keychain -p "$KEYPASS" "$KEYCHAIN"
  security set-keychain-settings -lut 21600 "$KEYCHAIN"
  security unlock-keychain -p "$KEYPASS" "$KEYCHAIN"
  security import "$RUNNER_TEMP/cert.p12" -P "${MACOS_CERT_PASSWORD:-}" -A -t cert -f pkcs12 -k "$KEYCHAIN"
  security set-key-partition-list -S apple-tool:,apple: -k "$KEYPASS" "$KEYCHAIN" > /dev/null
  security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')
  rm -f "$RUNNER_TEMP/cert.p12"
  codesign --force --timestamp --options runtime \
    --entitlements "$HERE/entitlements.plist" \
    --sign "$MACOS_SIGN_IDENTITY" "$APP"
  SIGNED=1
else
  echo "No signing certificate: ad-hoc signature (unsigned build)"
  codesign --force --deep --options runtime --entitlements "$HERE/entitlements.plist" --sign - "$APP"
  SIGNED=0
fi
codesign --verify --deep --strict --verbose=2 "$APP"

STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Aplicativos"
cp "$HERE/../../docs/INSTALACAO.md" "$STAGE/LEIA-ME.md"
rm -f "$DMG"
hdiutil create -volname "$NAME $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

if [[ "$SIGNED" == 1 ]]; then
  codesign --force --timestamp --sign "$MACOS_SIGN_IDENTITY" "$DMG"
  if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    echo "Notarizing"
    xcrun notarytool submit "$DMG" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
      --password "$APPLE_APP_PASSWORD" --wait
    xcrun stapler staple "$DMG"
    spctl -a -t open --context context:primary-signature -vv "$DMG"
  else
    echo "Signed but not notarized (APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD missing)"
  fi
fi
echo "Created $DMG"
