# Android Troubleshooting

## Build fails with missing headers

Make sure submodules are initialized:

```bash
git submodule update --init --recursive
```

## JitPack build fails

Common causes:
- Missing Gradle wrapper JAR in the repo
- Submodules not initialized

Trigger a new tag build after fixes. JitPack uses git tags as versions.

## Audio file fails to load

The engine expects an absolute file path. Copy `content://` URIs to a temp file first.

## Recording returns silence

Check that `RECORD_AUDIO` permission is granted at runtime and that the input device is available.
