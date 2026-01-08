# Expo Troubleshooting

## content:// URIs

On Android, `content://` URIs are not supported yet. Copy the asset to a file and pass a `file://` or absolute path instead.

## Recording errors

Ensure `RECORD_AUDIO` permission is granted at runtime.

## Background playback

If background playback does not continue, confirm the app is configured with
`UIBackgroundModes: ["audio"]` (iOS) and that notification permissions are
granted (Android).
