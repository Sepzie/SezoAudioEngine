package expo.modules.audioengine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.media.AudioManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.media.app.NotificationCompat as MediaStyleNotificationCompat
import android.support.v4.media.MediaMetadataCompat
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.session.PlaybackStateCompat

internal class MediaPlaybackService : Service() {
  private lateinit var mediaSession: MediaSessionCompat
  private val notificationManager by lazy { NotificationManagerCompat.from(this) }

  private var metadata = NowPlayingMetadata()
  private var cardOptions = PlaybackCardOptions()
  private var isPlaying = false
  private var isForeground = false
  private var noisyReceiverRegistered = false

  private val noisyReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
      if (intent?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY) {
        handlePause()
        publishState()
      }
    }
  }

  override fun onCreate() {
    super.onCreate()
    createNotificationChannel()
    mediaSession = MediaSessionCompat(this, TAG).apply {
      setFlags(
        MediaSessionCompat.FLAG_HANDLES_MEDIA_BUTTONS or
          MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS
      )
      setCallback(object : MediaSessionCompat.Callback() {
        override fun onPlay() {
          handlePlay()
          publishState()
        }

        override fun onPause() {
          handlePause()
          publishState()
        }

        override fun onStop() {
          handleStop()
          publishState()
        }

        override fun onSeekTo(pos: Long) {
          BackgroundPlaybackBridge.seekTo(pos.toDouble())
          publishState()
        }

        override fun onSkipToPrevious() {
          handleSeekBy(-cardOptions.seekStepMs)
          publishState()
        }

        override fun onSkipToNext() {
          handleSeekBy(cardOptions.seekStepMs)
          publishState()
        }
      })
      isActive = true
    }
    updatePlaybackState()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_PLAY -> handlePlay()
      ACTION_PAUSE -> handlePause()
      ACTION_TOGGLE -> if (isPlaying) handlePause() else handlePlay()
      ACTION_STOP -> handleStop()
      ACTION_PREVIOUS -> handleSeekBy(-cardOptions.seekStepMs)
      ACTION_NEXT -> handleSeekBy(cardOptions.seekStepMs)
      ACTION_STOP_SERVICE -> {
        stopSelf()
        return START_NOT_STICKY
      }
      ACTION_UPDATE, null -> {
        applyMetadataBundle(intent?.getBundleExtra(EXTRA_METADATA))
        if (intent?.hasExtra(EXTRA_IS_PLAYING) == true) {
          isPlaying = intent.getBooleanExtra(EXTRA_IS_PLAYING, isPlaying)
        }
      }
      else -> Log.w(TAG, "Unknown service action: ${intent.action}")
    }

    publishState()
    return START_STICKY
  }

  override fun onDestroy() {
    super.onDestroy()
    unregisterNoisyReceiver()
    if (isForeground) {
      stopForegroundCompat(removeNotification = true)
      isForeground = false
    } else {
      notificationManager.cancel(NOTIFICATION_ID)
    }
    mediaSession.release()
  }

  override fun onBind(intent: Intent?): IBinder? = null

  private fun handlePlay() {
    isPlaying = BackgroundPlaybackBridge.play()
  }

  private fun handlePause() {
    BackgroundPlaybackBridge.pause()
    isPlaying = false
  }

  private fun handleStop() {
    BackgroundPlaybackBridge.stop()
    isPlaying = false
  }

  private fun handleSeekBy(deltaMs: Long) {
    val duration = BackgroundPlaybackBridge.getDurationMs().coerceAtLeast(0.0)
    val current = BackgroundPlaybackBridge.getCurrentPositionMs().coerceAtLeast(0.0)
    val unclampedTarget = current + deltaMs.toDouble()
    val maxPosition = if (duration > 0.0) duration else Double.MAX_VALUE
    val target = unclampedTarget.coerceIn(0.0, maxPosition)
    BackgroundPlaybackBridge.seekTo(target)
  }

  private fun publishState() {
    isPlaying = BackgroundPlaybackBridge.isPlaying()
    updatePlaybackState()
    mediaSession.setMetadata(buildMediaMetadata())

    val notification = buildNotification()
    if (isPlaying) {
      registerNoisyReceiver()
      if (!isForeground) {
        startForeground(NOTIFICATION_ID, notification)
        isForeground = true
      } else {
        notificationManager.notify(NOTIFICATION_ID, notification)
      }
    } else {
      unregisterNoisyReceiver()
      if (isForeground) {
        stopForegroundCompat(removeNotification = false)
        isForeground = false
      }
      notificationManager.notify(NOTIFICATION_ID, notification)
    }
  }

  private fun stopForegroundCompat(removeNotification: Boolean) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      stopForeground(
        if (removeNotification) {
          Service.STOP_FOREGROUND_REMOVE
        } else {
          Service.STOP_FOREGROUND_DETACH
        }
      )
    } else {
      @Suppress("DEPRECATION")
      stopForeground(removeNotification)
    }
  }

  private fun updatePlaybackState() {
    val actions = PlaybackStateCompat.ACTION_PLAY or
      PlaybackStateCompat.ACTION_PAUSE or
      PlaybackStateCompat.ACTION_PLAY_PAUSE or
      PlaybackStateCompat.ACTION_STOP or
      PlaybackStateCompat.ACTION_SEEK_TO

    val state = if (isPlaying) {
      PlaybackStateCompat.STATE_PLAYING
    } else {
      PlaybackStateCompat.STATE_PAUSED
    }

    mediaSession.setPlaybackState(
      PlaybackStateCompat.Builder()
        .setActions(actions)
        .setState(
          state,
          BackgroundPlaybackBridge.getCurrentPositionMs().toLong(),
          if (isPlaying) 1.0f else 0.0f
        )
        .build()
    )
  }

  private fun buildMediaMetadata(): MediaMetadataCompat {
    val builder = MediaMetadataCompat.Builder()
      .putString(MediaMetadataCompat.METADATA_KEY_TITLE, metadata.title)
      .putString(MediaMetadataCompat.METADATA_KEY_ARTIST, metadata.artist ?: "")
      .putString(MediaMetadataCompat.METADATA_KEY_ALBUM, metadata.album ?: "")
      .putLong(
        MediaMetadataCompat.METADATA_KEY_DURATION,
        BackgroundPlaybackBridge.getDurationMs().toLong()
      )

    resolveArtworkBitmap()?.let { bitmap ->
      builder.putBitmap(MediaMetadataCompat.METADATA_KEY_ART, bitmap)
      builder.putBitmap(MediaMetadataCompat.METADATA_KEY_ALBUM_ART, bitmap)
      builder.putBitmap(MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON, bitmap)
    }
    return builder.build()
  }

  private fun buildNotification(): Notification {
    val style = MediaStyleNotificationCompat.MediaStyle()
      .setMediaSession(mediaSession.sessionToken)

    val builder = NotificationCompat.Builder(this, CHANNEL_ID)
      .setSmallIcon(resolveSmallIconResId())
      .setContentTitle(metadata.title.ifBlank { "Audio" })
      .setContentText(metadata.artist ?: "")
      .setSubText(metadata.album)
      .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
      .setOnlyAlertOnce(true)
      .setOngoing(isPlaying)
      .setShowWhen(false)
      .setStyle(style)

    buildContentIntent()?.let { builder.setContentIntent(it) }
    builder.setDeleteIntent(buildServicePendingIntent(ACTION_STOP))

    resolveArtworkBitmap()?.let { builder.setLargeIcon(it) }
    cardOptions.accentColor?.let { color ->
      builder.setColorized(true)
      builder.setColor(color)
    }

    val compactActions = mutableListOf<Int>()
    var actionIndex = 0

    if (cardOptions.showPrevious) {
      builder.addAction(
        android.R.drawable.ic_media_previous,
        "Back",
        buildServicePendingIntent(ACTION_PREVIOUS)
      )
      compactActions.add(actionIndex)
      actionIndex++
    }

    val playPauseAction = if (isPlaying) ACTION_PAUSE else ACTION_PLAY
    val playPauseIcon = if (isPlaying) android.R.drawable.ic_media_pause else android.R.drawable.ic_media_play
    val playPauseLabel = if (isPlaying) "Pause" else "Play"
    builder.addAction(playPauseIcon, playPauseLabel, buildServicePendingIntent(playPauseAction))
    compactActions.add(actionIndex)
    actionIndex++

    if (cardOptions.showNext) {
      builder.addAction(
        android.R.drawable.ic_media_next,
        "Forward",
        buildServicePendingIntent(ACTION_NEXT)
      )
      compactActions.add(actionIndex)
      actionIndex++
    }

    if (cardOptions.showStop) {
      builder.addAction(
        android.R.drawable.ic_menu_close_clear_cancel,
        "Stop",
        buildServicePendingIntent(ACTION_STOP)
      )
    }

    style.setShowActionsInCompactView(*compactActions.take(3).toIntArray())
    return builder.build()
  }

  private fun buildServicePendingIntent(action: String): PendingIntent {
    val intent = Intent(this, MediaPlaybackService::class.java).apply { this.action = action }
    val flags = PendingIntent.FLAG_UPDATE_CURRENT or
      (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0)
    return PendingIntent.getService(this, action.hashCode(), intent, flags)
  }

  private fun buildContentIntent(): PendingIntent? {
    val launchIntent = packageManager.getLaunchIntentForPackage(packageName) ?: return null
    launchIntent.flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
    val flags = PendingIntent.FLAG_UPDATE_CURRENT or
      (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0)
    return PendingIntent.getActivity(this, 1001, launchIntent, flags)
  }

  private fun applyMetadataBundle(bundle: Bundle?) {
    if (bundle == null) {
      return
    }

    bundle.getString(KEY_TITLE)?.let { metadata.title = it }
    if (bundle.containsKey(KEY_ARTIST)) {
      metadata.artist = bundle.getString(KEY_ARTIST)
    }
    if (bundle.containsKey(KEY_ALBUM)) {
      metadata.album = bundle.getString(KEY_ALBUM)
    }
    if (bundle.containsKey(KEY_ARTWORK)) {
      metadata.artwork = bundle.getString(KEY_ARTWORK)
    }
    if (bundle.containsKey(KEY_LOGO)) {
      metadata.logo = bundle.getString(KEY_LOGO)
    }

    val playbackCardBundle = bundle.getBundle(KEY_PLAYBACK_CARD)
    if (playbackCardBundle != null) {
      applyPlaybackCardBundle(playbackCardBundle)
    }
  }

  private fun applyPlaybackCardBundle(bundle: Bundle) {
    if (bundle.containsKey(KEY_CARD_SMALL_ICON)) {
      cardOptions.smallIcon = bundle.getString(KEY_CARD_SMALL_ICON)
    }
    if (bundle.containsKey(KEY_CARD_SHOW_PREVIOUS)) {
      cardOptions.showPrevious = bundle.getBoolean(KEY_CARD_SHOW_PREVIOUS)
    }
    if (bundle.containsKey(KEY_CARD_SHOW_NEXT)) {
      cardOptions.showNext = bundle.getBoolean(KEY_CARD_SHOW_NEXT)
    }
    if (bundle.containsKey(KEY_CARD_SHOW_STOP)) {
      cardOptions.showStop = bundle.getBoolean(KEY_CARD_SHOW_STOP)
    }
    if (bundle.containsKey(KEY_CARD_SEEK_STEP_MS)) {
      val seekValue = bundle.get(KEY_CARD_SEEK_STEP_MS)
      val seekStepMs = when (seekValue) {
        is Long -> seekValue
        is Int -> seekValue.toLong()
        is Double -> seekValue.toLong()
        is Float -> seekValue.toLong()
        is String -> seekValue.toLongOrNull()
        is Number -> seekValue.toLong()
        else -> null
      }
      if (seekStepMs != null) {
        cardOptions.seekStepMs = seekStepMs.coerceAtLeast(1000L)
      }
    }
    if (bundle.containsKey(KEY_CARD_ACCENT_COLOR)) {
      cardOptions.accentColor = parseColor(bundle.get(KEY_CARD_ACCENT_COLOR))
    }
  }

  private fun parseColor(value: Any?): Int? {
    return when (value) {
      is Int -> value
      is Long -> value.toInt()
      is Double -> value.toInt()
      is Float -> value.toInt()
      is String -> try {
        Color.parseColor(value)
      } catch (_: IllegalArgumentException) {
        null
      }
      else -> null
    }
  }

  private fun resolveArtworkBitmap(): Bitmap? {
    val artworkValue = metadata.artwork ?: metadata.logo ?: return null
    return decodeBitmapFromString(artworkValue)
  }

  private fun decodeBitmapFromString(value: String): Bitmap? {
    val path = when {
      value.startsWith("file://") -> Uri.parse(value).path
      value.startsWith("/") -> value
      else -> null
    }

    if (!path.isNullOrBlank()) {
      return BitmapFactory.decodeFile(path)
    }

    val resId = resolveResourceId(value)
    if (resId != 0) {
      return BitmapFactory.decodeResource(resources, resId)
    }
    return null
  }

  private fun resolveSmallIconResId(): Int {
    val customIconName = cardOptions.smallIcon ?: metadata.logo
    if (!customIconName.isNullOrBlank()) {
      val customResId = resolveResourceId(customIconName)
      if (customResId != 0) {
        return customResId
      }
    }

    val appIcon = applicationInfo.icon
    if (appIcon != 0) {
      return appIcon
    }
    return android.R.drawable.ic_media_play
  }

  private fun resolveResourceId(name: String): Int {
    val cleaned = name.substringAfterLast("/").substringBeforeLast(".")
    val drawable = resources.getIdentifier(cleaned, "drawable", packageName)
    if (drawable != 0) {
      return drawable
    }
    return resources.getIdentifier(cleaned, "mipmap", packageName)
  }

  private fun createNotificationChannel() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
      return
    }
    val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    if (manager.getNotificationChannel(CHANNEL_ID) != null) {
      return
    }

    val channel = NotificationChannel(
      CHANNEL_ID,
      CHANNEL_NAME,
      NotificationManager.IMPORTANCE_LOW
    ).apply {
      description = CHANNEL_DESCRIPTION
      lockscreenVisibility = Notification.VISIBILITY_PUBLIC
    }
    manager.createNotificationChannel(channel)
  }

  private fun registerNoisyReceiver() {
    if (noisyReceiverRegistered) {
      return
    }
    registerReceiver(noisyReceiver, IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY))
    noisyReceiverRegistered = true
  }

  private fun unregisterNoisyReceiver() {
    if (!noisyReceiverRegistered) {
      return
    }
    try {
      unregisterReceiver(noisyReceiver)
    } catch (_: IllegalArgumentException) {
      // Receiver was already unregistered.
    }
    noisyReceiverRegistered = false
  }

  private data class NowPlayingMetadata(
    var title: String = "Audio",
    var artist: String? = null,
    var album: String? = null,
    var artwork: String? = null,
    var logo: String? = null
  )

  private data class PlaybackCardOptions(
    var smallIcon: String? = null,
    var accentColor: Int? = null,
    var showPrevious: Boolean = false,
    var showNext: Boolean = false,
    var showStop: Boolean = true,
    var seekStepMs: Long = 15000L
  )

  companion object {
    private const val TAG = "MediaPlaybackService"
    private const val CHANNEL_ID = "sezo_audio_playback"
    private const val CHANNEL_NAME = "Audio playback"
    private const val CHANNEL_DESCRIPTION = "Playback controls"
    private const val NOTIFICATION_ID = 7342

    const val ACTION_UPDATE = "expo.modules.audioengine.action.UPDATE"
    const val ACTION_PLAY = "expo.modules.audioengine.action.PLAY"
    const val ACTION_PAUSE = "expo.modules.audioengine.action.PAUSE"
    const val ACTION_TOGGLE = "expo.modules.audioengine.action.TOGGLE"
    const val ACTION_STOP = "expo.modules.audioengine.action.STOP"
    const val ACTION_PREVIOUS = "expo.modules.audioengine.action.PREVIOUS"
    const val ACTION_NEXT = "expo.modules.audioengine.action.NEXT"
    const val ACTION_STOP_SERVICE = "expo.modules.audioengine.action.STOP_SERVICE"

    private const val EXTRA_METADATA = "extra_metadata"
    private const val EXTRA_IS_PLAYING = "extra_is_playing"

    private const val KEY_TITLE = "title"
    private const val KEY_ARTIST = "artist"
    private const val KEY_ALBUM = "album"
    private const val KEY_ARTWORK = "artwork"
    private const val KEY_LOGO = "logo"
    private const val KEY_PLAYBACK_CARD = "playbackCard"
    private const val KEY_CARD_SMALL_ICON = "smallIcon"
    private const val KEY_CARD_ACCENT_COLOR = "accentColor"
    private const val KEY_CARD_SHOW_PREVIOUS = "showPrevious"
    private const val KEY_CARD_SHOW_NEXT = "showNext"
    private const val KEY_CARD_SHOW_STOP = "showStop"
    private const val KEY_CARD_SEEK_STEP_MS = "seekStepMs"

    fun sync(context: Context, metadata: Bundle, isPlaying: Boolean) {
      val intent = Intent(context, MediaPlaybackService::class.java).apply {
        action = ACTION_UPDATE
        putExtra(EXTRA_METADATA, metadata)
        putExtra(EXTRA_IS_PLAYING, isPlaying)
      }

      try {
        if (isPlaying) {
          ContextCompat.startForegroundService(context, intent)
        } else {
          context.startService(intent)
        }
      } catch (e: Exception) {
        Log.e(TAG, "Failed to start/update playback service", e)
      }
    }

    fun stop(context: Context) {
      val stopIntent = Intent(context, MediaPlaybackService::class.java).apply {
        action = ACTION_STOP_SERVICE
      }
      try {
        context.startService(stopIntent)
      } catch (e: Exception) {
        Log.w(TAG, "Failed to request service stop", e)
      }
    }
  }
}
