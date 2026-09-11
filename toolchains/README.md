# Repository toolchains

This directory is the single home for the repository's materialized compiler
toolchains and its vendored Java bundle. The Windows build wrappers use
`mingw32/`, `mingw64/`, and `emsdk-win64/`; the RuneLite lane uses the Java
archive and `unpacked/`. This directory is intentionally not gitignored.

## Vendored Java toolchain

`java-toolchain-osrs239.zip` is everything Java that `run-runelite.sh` needs and
that lives in no repository: a JDK, RuneLite's own jar repository, and the two
reference jars the deob build and its API gate read.

```
jdk/temurin-21.jdk/            the JDK — bundle, not just Contents/Home
runelite/repository2/          RuneLite 1.12.33's jars (client, api, libraries)
deob/runelite-api-1.12.33-runtime.jar   instr/build.sh compiles against this
deob/injected-client-1.12.33.jar        runelite_patch.py patches this
deob/gamepack-deob.jar                  verify_api.py's API baseline
MANIFEST.txt                   where each came from, and its sha256
```

## Using it

`run-runelite.sh` unpacks it to `toolchains/unpacked/` by itself, but only when
the system installs are missing — an installed JDK and a real
`~/.runelite/repository2` always win, so unzipping this on a working machine
changes nothing.

To use it by hand:

```sh
unzip -q toolchains/java-toolchain-osrs239.zip -d toolchains/unpacked
JAVA_HOME=$PWD/toolchains/unpacked/jdk/temurin-21.jdk/Contents/Home \
RL_REPO=$PWD/toolchains/unpacked/runelite/repository2 \
  ./run-runelite.sh
```

Or install the JDK where the system finds it:

```sh
sudo ditto toolchains/unpacked/jdk/temurin-21.jdk \
    /Library/Java/JavaVirtualMachines/temurin-21.jdk
```

## Why 1.12.33 specifically

It is the RuneLite release that ships a revision-**239** client. 1.12.34 and
later ship 240, and a client of one revision cannot log in to a server speaking
another — the same reason two `client-*.jar` or two `runelite-api-*.jar` must
never share a classpath (both carry `logback.xml` and
`net.runelite.client.RuneLite`, so whichever sorts first silently wins).

## Repacking

```sh
toolchains/vendor_java.sh              # -> toolchains/java-toolchain-osrs239.zip
```

Overridable with `JAVA_HOME`, `RL_REPO`, `DEOB_REPO`, `RL_VERSION`.

## What is deliberately absent

`Deobfuscator.jar` and the obfuscated `gamepack.jar` are needed to
**re-decompile** a gamepack, not to compile `instr/src` — which is all
`run-runelite.sh` does. Gradle is absent for the same reason: the Deobfuscator's
`gradlew` fetches its own.

## Size, and git

The zip is a few hundred megabytes and is tracked through Git LFS. Materialized
toolchains are visible to Git as well, so review them deliberately when adding
changes.
