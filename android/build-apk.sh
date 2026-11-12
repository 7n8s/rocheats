#!/usr/bin/env bash
# Builds RoCheatsMonitor.apk with nothing but the Android SDK build-tools + d8.
#   ANDROID_HOME must point at an SDK with platforms;android-34 and build-tools;34.0.0
#   JAVA_HOME   must be a JDK 17 (aapt2/d8 are Java tools)
set -euo pipefail
cd "$(dirname "$0")"

SDK=${ANDROID_HOME:?set ANDROID_HOME}
BT=$SDK/build-tools/34.0.0
JAR=$SDK/platforms/android-34/android.jar
OUT=build
rm -rf $OUT && mkdir -p $OUT/classes $OUT/dex $OUT/res

echo "[1/6] resources"
"$BT/aapt2" compile --dir res -o $OUT/res.zip

echo "[2/6] link"
"$BT/aapt2" link -o $OUT/base.apk -I "$JAR" \
    --manifest AndroidManifest.xml \
    --min-sdk-version 23 --target-sdk-version 34 \
    --version-code 3 --version-name 2.2.0 \
    $OUT/res.zip
(cd $OUT && unzip -qo base.apk classes.dex 2>/dev/null || true)

echo "[3/6] javac"
find src -name '*.java' > $OUT/sources.txt
javac -encoding UTF-8 -source 8 -target 8 -classpath "$JAR" \
      -d $OUT/classes @$OUT/sources.txt 2>&1 | grep -v "bootstrap class path" || true

echo "[4/6] d8"
"$BT/d8" --release --min-api 23 --lib "$JAR" --output $OUT/dex \
      $(find $OUT/classes -name '*.class')

echo "[5/6] package"
cp $OUT/base.apk $OUT/unsigned.apk
(cd $OUT && zip -qj unsigned.apk dex/classes.dex)

echo "[6/6] align + sign"
"$BT/zipalign" -f -p 4 $OUT/unsigned.apk $OUT/aligned.apk
KS=${ANDROID_KEYSTORE:-keystore.jks}
if [ ! -f "$KS" ]; then
  keytool -genkeypair -keystore $KS -alias rcm -keyalg RSA -keysize 2048 \
          -validity 10950 -storepass rocheats -keypass rocheats \
          -dname "CN=RoCheats Monitor, O=RoCheats, C=GB" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks $KS --ks-pass pass:rocheats --key-pass pass:rocheats \
      --v1-signing-enabled true --v2-signing-enabled true \
      --out ../RoCheatsMonitor.apk $OUT/aligned.apk
"$BT/apksigner" verify --print-certs ../RoCheatsMonitor.apk | head -3
ls -l ../RoCheatsMonitor.apk
# Note: never run jarsigner on the result afterwards. It rewrites META-INF and
# breaks the v2 scheme (targetSdk 34 requires v2), so the APK stops installing.
echo "OK  ->  $(cd .. && pwd)/RoCheatsMonitor.apk"
