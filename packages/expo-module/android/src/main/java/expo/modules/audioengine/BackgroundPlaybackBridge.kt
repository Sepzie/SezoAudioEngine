package expo.modules.audioengine

internal object BackgroundPlaybackBridge {
  interface Controller {
    fun play(): Boolean
    fun pause()
    fun stop()
    fun seekTo(positionMs: Double)
    fun isPlaying(): Boolean
    fun getCurrentPositionMs(): Double
    fun getDurationMs(): Double
  }

  @Volatile
  private var controller: Controller? = null

  fun setController(nextController: Controller?) {
    controller = nextController
  }

  fun play(): Boolean = controller?.play() ?: false

  fun pause() {
    controller?.pause()
  }

  fun stop() {
    controller?.stop()
  }

  fun seekTo(positionMs: Double) {
    controller?.seekTo(positionMs)
  }

  fun isPlaying(): Boolean = controller?.isPlaying() ?: false

  fun getCurrentPositionMs(): Double = controller?.getCurrentPositionMs() ?: 0.0

  fun getDurationMs(): Double = controller?.getDurationMs() ?: 0.0
}
