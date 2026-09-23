#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "Silver Security needs XcodeGen. Install it on macOS, then rerun this script." >&2
  exit 2
fi
if ! command -v xcodebuild >/dev/null 2>&1; then
  echo "Silver Security needs Xcode and xcodebuild. Open this project on macOS." >&2
  exit 2
fi

xcodegen generate
xcodebuild \
  -project SilverSecurity.xcodeproj \
  -scheme SilverSecurity \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' \
  build

echo "Silver Security simulator build succeeded. Use a signed physical iPhone for BLE validation."
