# Expo Troubleshooting

## content:// URIs

On Android, `content://` URIs are not supported yet. Copy the asset to a file and pass a `file://` or absolute path instead.

## Recording errors

Ensure `RECORD_AUDIO` permission is granted at runtime.

## Background playback

The background playback helpers are currently no-ops. Implement MediaSession and a foreground service at the app layer for now.
