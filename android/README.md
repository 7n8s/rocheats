# RoCheats Monitor for Android

The same checker as the desktop build, same design tokens, same nine products, same
WEAO feed — packaged as a real APK.

```
RoCheatsMonitor.apk        installable, signed, 247 KB, minSdk 23, targetSdk 34
```

## What it does

- Polls `https://weao.xyz/api/status/exploits` with the `WEAO-3PService` user agent
  (matching the desktop build), maps the nine tracked products, and shows
  **updated / not updated / detected** the same way.
- Splash: blurred RC behind **ROCHEATS STATUS CHECKER**, hold then dissolve.
- Rows are inert. Only the detection badge (mute per product), the gear, the filter
  chips and *Check now* do anything.
- Settings sheet: 7 scenes, accent presets, glass/flat, check interval, notifications,
  sound. Saved in SharedPreferences.
- Background checks notify you when a product changes state (per-product mute respected).

## Build

Needs a JDK 17 and an SDK with `platforms;android-34` and `build-tools;34.0.0`:

```
export JAVA_HOME=/path/to/jdk-17 ANDROID_HOME=/path/to/sdk
bash android/build-apk.sh          # -> RoCheatsMonitor.apk in the repo root
```

No Gradle: `aapt2` → `javac` → `d8` → `zipalign` → `apksigner`. The signing key is
`android/keystore.jks` (pass `rocheats`) — self-signed, so Android will ask you to
allow installs from your browser/file manager.

## Looking at it without a device

`android/desktop-preview/` stubs the ~20 Android classes the UI touches onto Java2D, so
the *real* `MainView`/`SplashView` draw into a PNG on a laptop:

```
cd android/desktop-preview
javac -encoding UTF-8 -d out harness/Render.java ../src/com/rocheats/monitor/*.java $(find stubs -name '*.java')
java -cp out com.rocheats.monitor.Render ../preview 412 2.75
```

It also asserts that tapping a product row opens nothing and that the badge toggles.

`harness/TestWatch.java` is the alerting test — live feed plus simulated changes:

```
java -cp out:lib/json.jar com.rocheats.monitor.TestWatch
```

It checks a first poll is silent, an unchanged poll is silent, stale/detected/new-version
events fire, muting silences exactly one product, and a new Roblox build alerts once then
goes quiet. Needs `lib/json.jar` (org.json), fetched from Maven Central.

## Roblox update detection

Roblox ships client builds without warning, so both builds watch the client
version directly instead of waiting for the next product poll:

```
GET https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer
-> {"version":"0.747.0.7471481","clientVersionUpload":"version-4ea8e85481e34f46"}
```

- **APK:** polls it every **20 s** while open, plus on every check.
- **Windows:** same endpoint on a 20 s timer, from the tray too.
- A change fires one high-priority **"Roblox updated"** notification (its own
  channel, so it can be muted separately), pulses the strip dot, and forces an
  immediate feed refresh because every product's currency flag just changed.
- Each product's `rbxversion` is compared with the live upload hash: the hero
  shows *"N behind Roblox"* and a lagging row gets an amber `▲` after its version.

## Feed fields it reads

Straight off the live payload, same as the desktop build:

| field | use |
|---|---|
| `title` | matched case-insensitively against the nine products |
| `updateStatus` | the green **Updated** / red **Not updated** word |
| `detected` | the corner badge: an amber `!` on the artwork when detected, hidden when not |
| `version` | the version line ("unknown" is treated as absent) |
| `updatedDate` | `11/06/2026 at 1:24 PM UTC` → `3m ago` |
| `rbxversion` | the Roblox build it supports → *current* or *behind* |

Notification mutes are per product and survive restarts.
