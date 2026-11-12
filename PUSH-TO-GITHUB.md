# Getting this onto GitHub

The repo is already committed locally (`/home/user/RoCheatsMonitor`), with two
commits, a `.gitignore`, `.gitattributes`, MIT `LICENSE` and the build outputs
excluded. You just need to create the empty repo and push.

I can't do this half myself: this workspace has no GitHub connection and no
credentials, so pushing has to come from your machine.

---

## Route A — GitHub Desktop (easiest, no commands)

1. Download the repo as a zip from the workspace and unzip it somewhere, e.g.
   `C:\Users\<you>\Documents\RoCheatsMonitor`.
2. Install **GitHub Desktop**, sign in.
3. `File → Add local repository…` → choose that folder. It will say "this
   directory does not appear to be a Git repository" if you grabbed the folder
   without the hidden `.git` directory — in that case use Route B, or
   `File → New repository…` with the same name and let it initialise, then copy
   the files in and commit.
4. Click **Publish repository** (top bar). Untick *Keep this code private* if you
   want it public.

## Route B — command line

Create an **empty** repo on github.com first (no README, no .gitignore, no
licence — otherwise the first push is rejected), then:

```bash
cd /path/to/RoCheatsMonitor
git remote add origin https://github.com/<your-username>/rocheats-monitor.git
git branch -M main
git push -u origin main
```

GitHub will ask for a password: use a **personal access token**
(*Settings → Developer settings → Personal access tokens → Tokens (classic) →*
scope `repo`), not your account password.

If you'd rather upload without git at all: create the empty repo, then
`Add file → Upload files` and drag the folder contents in. You'll lose the commit
history but the tree will be identical.

---

## Name suggestions

`rocheats-monitor` is the safe default. If you want it to read as a status page
rather than a tool, `rocheats-status` also works and matches the app's window
title.

## What is and isn't in the repo

Included: all three source trees (`src/`, `android/src/`, `web/`), build scripts,
resources, preview screenshots, READMEs and the deploy notes for the web build.

Excluded on purpose:

| Path | Why |
| --- | --- |
| `android/keystore.jks` | signing key — **never** commit this |
| `*.exe`, `*.apk`, `*.zip` | build output; publish them as GitHub **Releases** instead |
| `android/build/`, `*.class` | compiler output |

## Turning the exe and apk into a Release

Once the repo exists:

1. **Releases → Draft a new release**, tag `v1.0.0`.
2. Attach `RoCheatsMonitor.exe` and `RoCheatsMonitor.apk`.
3. Paste this into the notes if you want a starting point:

```markdown
First public build of RoCheats Monitor — Windows, Android and web.

**Windows** — download `RoCheatsMonitor.exe` and run it. Not code-signed, so
Windows shows "Windows protected your PC": More info → Run anyway.
**Android** — download `RoCheatsMonitor.apk` and allow install from unknown
sources. Requires Android 8.0+.
**Web** — nothing to download; opens in any browser.

Watches nine products via the WEAO API plus the live Roblox client build. The
amber `!` on a product's artwork means it is currently detected — hover it
(the pointer) or tap it (phone) to read it. Clicking the badge mutes alerts for
that one product.
```

## About the APK and the keystore

The APK in the workspace is signed with `android/keystore.jks`, which is
deliberately **not** in the repo. Two consequences worth deciding on before you
publish anything:

- Keep the key somewhere private (password manager, encrypted backup). If it's
  lost you can never publish an update that installs over an existing install —
  Android will refuse it as a different signer.
- If you'd rather let others build it themselves, add the keystore path/password
  to the README as a *placeholder* and tell people to generate their own with
  `keytool`; `android/README.md` has the exact command.

If you'd prefer the repo not to carry the Android build at all yet, delete the
`android/` directory from the commit and add it back when the keystore question
is settled — nothing else depends on it.
