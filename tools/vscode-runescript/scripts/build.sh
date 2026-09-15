#!/usr/bin/env bash
#
# Build the language server and package the extension as a .vsix.
#
#   tools/vscode-runescript/scripts/build.sh            server + vsix
#   tools/vscode-runescript/scripts/build.sh --no-server   vsix only
#
# A .vsix is a zip with two generated manifests beside the extension's own
# files, so it is written here rather than with `vsce`: vsce is an npm install
# and a network round trip, and the manifest it produces for an extension this
# small is the one below.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXT_DIR="$(dirname "$HERE")"
REPO_ROOT="$(cd "$EXT_DIR/../.." && pwd)"
OUT_DIR="$EXT_DIR/dist"

BUILD_SERVER=1
for arg in "$@"; do
    case "$arg" in
        --no-server) BUILD_SERVER=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

read_field() {
    # One field out of package.json, without needing jq.
    node -e "process.stdout.write(String(require('$EXT_DIR/package.json').$1))"
}

NAME="$(read_field name)"
VERSION="$(read_field version)"
PUBLISHER="$(read_field publisher)"
DISPLAY_NAME="$(read_field displayName)"
DESCRIPTION="$(read_field description)"
VSIX="$OUT_DIR/$PUBLISHER.$NAME-$VERSION.vsix"

if [ "$BUILD_SERVER" = 1 ]; then
    echo "==> building runescript-lsp"
    if command -v make >/dev/null 2>&1; then
        make -C "$REPO_ROOT/tools/runescript-lsp"
    else
        cmake -S "$REPO_ROOT/tools/runescript-lsp" -B "$REPO_ROOT/build-lsp"
        cmake --build "$REPO_ROOT/build-lsp" --config Release
    fi
fi

echo "==> packaging $VSIX"
rm -rf "$OUT_DIR/stage"
mkdir -p "$OUT_DIR/stage/extension"

cp -R "$EXT_DIR/package.json" \
      "$EXT_DIR/extension.js" \
      "$EXT_DIR/language-configuration.json" \
      "$EXT_DIR/language-configuration-config.json" \
      "$EXT_DIR/syntaxes" \
      "$OUT_DIR/stage/extension/"
[ -f "$EXT_DIR/README.md" ] && cp "$EXT_DIR/README.md" "$OUT_DIR/stage/extension/"

# The two files a VSIX needs beyond the extension itself. The manifest is the
# marketplace's format; VS Code reads it to know what it is installing.
cat > "$OUT_DIR/stage/[Content_Types].xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json"/>
  <Default Extension="js" ContentType="application/javascript"/>
  <Default Extension="md" ContentType="text/markdown"/>
  <Default Extension="vsixmanifest" ContentType="text/xml"/>
</Types>
XML

cat > "$OUT_DIR/stage/extension.vsixmanifest" <<XML
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="$NAME" Version="$VERSION" Publisher="$PUBLISHER"/>
    <DisplayName>$DISPLAY_NAME</DisplayName>
    <Description xml:space="preserve">$DESCRIPTION</Description>
    <Tags>runescript,rs2,cs2</Tags>
    <Categories>Programming Languages</Categories>
    <GalleryFlags>Public</GalleryFlags>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
XML

rm -f "$VSIX"
( cd "$OUT_DIR/stage" && zip -r -q "$VSIX" . )
rm -rf "$OUT_DIR/stage"

echo "==> $VSIX"
echo "    install it with: tools/vscode-runescript/scripts/install.sh"
