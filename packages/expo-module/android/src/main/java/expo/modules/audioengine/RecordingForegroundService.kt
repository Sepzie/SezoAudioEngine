package expo.modules.audioengine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

/**
 * Foreground service that keeps the process (and microphone access) alive while
 * a recording is in progress, independently of background playback.
 *
 * It is separate from [MediaPlaybackService] on purpose: that service follows
 * the background-playback toggle and the play/pause state (it demotes on pause
 * and stops on stop), while this one follows only startRecording/stopRecording.
 * Keeping them apart also lets apps that never record drop this service and the
 * FOREGROUND_SERVICE_MICROPHONE permission from their manifest through the config
 * plugin (`android.recordingForegroundService: false`).
 *
 * Android 14+ only allows a `microphone` foreground service to start while the
 * app is in the foreground and RECORD_AUDIO is granted; the module checks both
 * before it starts this service from startRecording.
 */
internal class RecordingForegroundService : Service() {
  private var isForeground = false

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    if (intent?.action == ACTION_STOP) {
      demoteAndStop(startId)
      return START_NOT_STICKY
    }

    val includePlayback = intent?.getBooleanExtra(EXTRA_INCLUDE_PLAYBACK, false) ?: false
    promote(includePlayback)
    // A recording cannot survive a process restart, so never restart the service.
    return START_NOT_STICKY
  }

  override fun onDestroy() {
    if (isForeground) {
      ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
      isForeground = false
    }
    super.onDestroy()
  }

  override fun onBind(intent: Intent?): IBinder? = null

  private fun promote(includePlayback: Boolean) {
    createNotificationChannel()
    val notification = buildNotification()
    val requestedType = if (includePlayback) {
      FOREGROUND_SERVICE_TYPE_MICROPHONE or FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
    } else {
      FOREGROUND_SERVICE_TYPE_MICROPHONE
    }

    val microphoneError = tryStartForeground(notification, requestedType)
    if (microphoneError == null) {
      isForeground = true
      statusListener?.onStarted(requestedType)
      return
    }
    // SecurityException (RECORD_AUDIO revoked, FOREGROUND_SERVICE_MICROPHONE
    // removed from the manifest) or a while-in-use restriction.
    statusListener?.onFailed("Microphone foreground service rejected: ${microphoneError.message}")

    // The service was started with startForegroundService(), so it must call
    // startForeground() or the system crashes the app. Fall back to the
    // mediaPlayback type, which keeps the process alive (the system may still
    // silence microphone capture while the app is in the background).
    if (tryStartForeground(notification, FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK) == null) {
      isForeground = true
      statusListener?.onStarted(FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
    } else {
      stopSelf()
    }
  }

  /** Returns null on success, or the error the system threw. */
  private fun tryStartForeground(notification: Notification, type: Int): Exception? {
    return try {
      ServiceCompat.startForeground(this, NOTIFICATION_ID, notification, type)
      null
    } catch (error: Exception) {
      Log.e(TAG, "startForeground(type=$type) failed", error)
      error
    }
  }

  private fun demoteAndStop(startId: Int) {
    if (isForeground) {
      ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
      isForeground = false
    }
    // Only stop when no newer START (the next take) has arrived in the meantime;
    // that START promotes the service again.
    stopSelf(startId)
  }

  private fun buildNotification(): Notification {
    val builder = NotificationCompat.Builder(this, CHANNEL_ID)
      .setSmallIcon(resolveSmallIconResId())
      .setContentTitle(resolveString(STRING_TITLE, "Recording"))
      .setContentText(resolveString(STRING_TEXT, "Recording in progress"))
      .setOngoing(true)
      .setOnlyAlertOnce(true)
      .setShowWhen(true)
      .setUsesChronometer(true)
      .setWhen(System.currentTimeMillis())
      .setCategory(NotificationCompat.CATEGORY_SERVICE)
      .setPriority(NotificationCompat.PRIORITY_LOW)
      .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
      .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)

    buildContentIntent()?.let { builder.setContentIntent(it) }
    return builder.build()
  }

  private fun buildContentIntent(): PendingIntent? {
    val launchIntent = packageManager.getLaunchIntentForPackage(packageName) ?: return null
    launchIntent.flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
    val flags = PendingIntent.FLAG_UPDATE_CURRENT or
      (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0)
    return PendingIntent.getActivity(this, CONTENT_REQUEST_CODE, launchIntent, flags)
  }

  // Apps can override the texts and icon by defining resources with these names.
  private fun resolveString(name: String, fallback: String): String {
    val resId = resources.getIdentifier(name, "string", packageName)
    return if (resId != 0) getString(resId) else fallback
  }

  private fun resolveSmallIconResId(): Int {
    val custom = resources.getIdentifier(DRAWABLE_ICON, "drawable", packageName)
    if (custom != 0) {
      return custom
    }
    val appIcon = applicationInfo.icon
    if (appIcon != 0) {
      return appIcon
    }
    return android.R.drawable.ic_btn_speak_now
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
      resolveString(STRING_CHANNEL_NAME, "Recording"),
      NotificationManager.IMPORTANCE_LOW
    ).apply {
      setShowBadge(false)
      lockscreenVisibility = Notification.VISIBILITY_PUBLIC
    }
    manager.createNotificationChannel(channel)
  }

  /** Receives the outcome of a promotion so the module can emit events. */
  interface StatusListener {
    fun onStarted(foregroundServiceType: Int)
    fun onFailed(message: String)
  }

  companion object {
    private const val TAG = "RecordingFgService"
    private const val CHANNEL_ID = "sezo_audio_recording"
    private const val NOTIFICATION_ID = 7343
    private const val CONTENT_REQUEST_CODE = 1002

    private const val ACTION_START = "expo.modules.audioengine.action.RECORDING_START"
    private const val ACTION_STOP = "expo.modules.audioengine.action.RECORDING_STOP"
    private const val EXTRA_INCLUDE_PLAYBACK = "extra_include_playback"

    private const val STRING_TITLE = "sezo_audio_engine_recording_notification_title"
    private const val STRING_TEXT = "sezo_audio_engine_recording_notification_text"
    private const val STRING_CHANNEL_NAME = "sezo_audio_engine_recording_channel_name"
    private const val DRAWABLE_ICON = "sezo_audio_engine_recording_icon"

    // Values of ServiceInfo.FOREGROUND_SERVICE_TYPE_* (API 29/30); spelled out so
    // they are usable with minSdk 24. ServiceCompat masks them on older APIs.
    const val FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK = 0x00000002
    const val FOREGROUND_SERVICE_TYPE_MICROPHONE = 0x00000080

    @Volatile
    var statusListener: StatusListener? = null

    /** True when the app's merged manifest still declares this service. */
    fun isDeclared(context: Context): Boolean {
      val component = ComponentName(context, RecordingForegroundService::class.java)
      return try {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
          context.packageManager.getServiceInfo(component, PackageManager.ComponentInfoFlags.of(0))
        } else {
          @Suppress("DEPRECATION")
          context.packageManager.getServiceInfo(component, 0)
        }
        true
      } catch (_: PackageManager.NameNotFoundException) {
        false
      }
    }

    /**
     * Starts the service in the foreground. Must be called while the app is in
     * the foreground. Returns false when the system refused the start (for
     * example ForegroundServiceStartNotAllowedException on Android 12+).
     */
    fun start(context: Context, includePlayback: Boolean): Boolean {
      val intent = Intent(context, RecordingForegroundService::class.java).apply {
        action = ACTION_START
        putExtra(EXTRA_INCLUDE_PLAYBACK, includePlayback)
      }
      return try {
        ContextCompat.startForegroundService(context, intent)
        true
      } catch (error: Exception) {
        Log.e(TAG, "Failed to start recording service", error)
        false
      }
    }

    fun stop(context: Context) {
      // Prefer a STOP intent: it is delivered after a still-pending START, so a
      // service started with startForegroundService() always reaches
      // startForeground() before it stops (stopping it earlier crashes the app).
      // An app with a foreground service may call startService() from the
      // background; if the system refuses anyway, stop the service directly.
      val intent = Intent(context, RecordingForegroundService::class.java).apply {
        action = ACTION_STOP
      }
      try {
        context.startService(intent)
      } catch (error: Exception) {
        Log.w(TAG, "STOP intent refused, stopping recording service directly", error)
        try {
          context.stopService(Intent(context, RecordingForegroundService::class.java))
        } catch (stopError: Exception) {
          Log.w(TAG, "Failed to stop recording service", stopError)
        }
      }
    }
  }
}
