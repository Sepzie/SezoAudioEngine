package expo.modules.audioengine

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build

internal class AudioFocusManager(
  context: Context,
  private val onAudioFocusChanged: (Int) -> Unit
) {
  private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
  private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
    onAudioFocusChanged(change)
  }

  private var focusRequest: AudioFocusRequest? = null
  @Volatile
  private var hasFocus = false

  fun requestFocus(): Boolean {
    if (hasFocus) {
      return true
    }

    val result = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      if (focusRequest == null) {
        val attributes = AudioAttributes.Builder()
          .setUsage(AudioAttributes.USAGE_MEDIA)
          .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
          .build()
        focusRequest = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
          .setAudioAttributes(attributes)
          .setAcceptsDelayedFocusGain(false)
          .setWillPauseWhenDucked(false)
          .setOnAudioFocusChangeListener(focusListener)
          .build()
      }
      audioManager.requestAudioFocus(focusRequest!!)
    } else {
      @Suppress("DEPRECATION")
      audioManager.requestAudioFocus(
        focusListener,
        AudioManager.STREAM_MUSIC,
        AudioManager.AUDIOFOCUS_GAIN
      )
    }

    hasFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    return hasFocus
  }

  fun abandonFocus() {
    if (!hasFocus) {
      return
    }

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      focusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
    } else {
      @Suppress("DEPRECATION")
      audioManager.abandonAudioFocus(focusListener)
    }
    hasFocus = false
  }

  fun hasAudioFocus(): Boolean = hasFocus
}
