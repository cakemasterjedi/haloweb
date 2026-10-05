package com.emblem.auto

import android.net.Uri
import android.os.Bundle
import android.support.v4.media.MediaBrowserCompat.MediaItem
import android.support.v4.media.MediaDescriptionCompat
import android.support.v4.media.MediaMetadataCompat
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.session.PlaybackStateCompat
import androidx.media.MediaBrowserServiceCompat
import java.util.concurrent.Executors

/**
 * Android Auto: the emblem's designs as a media library. Two tabs (the
 * built-in designs and your pictures), shown as grids of tiles; "playing"
 * a tile shows that design on the emblem. Next steps to the next design,
 * the two custom buttons make it brighter or dimmer. No sound is played.
 */
class EmblemMediaService : MediaBrowserServiceCompat() {
    private lateinit var session: MediaSessionCompat
    private val io = Executors.newSingleThreadExecutor()
    @Volatile private var last: EmblemState? = null
    private val art = HashMap<String, Uri>()  // mediaId -> tile picture

    override fun onCreate() {
        super.onCreate()
        Emblem.init(this)
        session = MediaSessionCompat(this, "Emblem").apply {
            setCallback(Callback())
            setPlaybackState(playback(PlaybackStateCompat.STATE_PAUSED))
            isActive = true
        }
        sessionToken = session.sessionToken
    }

    override fun onDestroy() {
        session.release()
        io.shutdown()
        super.onDestroy()
    }

    // ---- browsing

    override fun onGetRoot(clientPackageName: String, clientUid: Int, rootHints: Bundle?): BrowserRoot {
        val extras = Bundle().apply {
            putBoolean(CONTENT_STYLE_SUPPORTED, true)
            putInt(CONTENT_STYLE_BROWSABLE, STYLE_GRID)
            putInt(CONTENT_STYLE_PLAYABLE, STYLE_GRID)
        }
        return BrowserRoot(ROOT, extras)
    }

    override fun onLoadChildren(parentId: String, result: Result<MutableList<MediaItem>>) {
        if (parentId == ROOT) {
            result.sendResult(mutableListOf(tab(TAB_DESIGNS, "Designs"), tab(TAB_PICTURES, "My pictures")))
            return
        }
        result.detach()
        io.execute {
            val items = mutableListOf<MediaItem>()
            try {
                val s = connect()
                if (parentId == TAB_DESIGNS) {
                    for (d in EmblemState.BUILT_IN) items += tile(d, s)
                } else {
                    for (d in s.designs().filter { it.mode == EmblemState.MODE_IMAGE }) {
                        Emblem.thumb(d.slot)?.let { art[id(d)] = Art.thumbnail(this, d.slot, it) }
                        items += tile(d, s)
                    }
                    if (items.isEmpty()) items += message("No pictures yet", "Add them on the emblem's web page")
                }
            } catch (e: Exception) {
                items += message("Emblem not found", "Turn on your hotspot and the emblem · tap to try again")
            }
            result.sendResult(items)
        }
    }

    /** Finds the emblem if needed and reads its state (on io). */
    private fun connect(): EmblemState {
        val host = Emblem.address
        if (host == null || !Emblem.probe(host)) Emblem.find() ?: throw java.io.IOException("not found")
        return Emblem.state().also { last = it }
    }

    private fun tab(id: String, title: String) = MediaItem(
        MediaDescriptionCompat.Builder().setMediaId(id).setTitle(title).build(), MediaItem.FLAG_BROWSABLE,
    )

    private fun message(title: String, subtitle: String) = MediaItem(
        MediaDescriptionCompat.Builder().setMediaId(RETRY).setTitle(title).setSubtitle(subtitle)
            .setIconUri(Art.resource(this, R.drawable.design_roundel, "design_roundel")).build(),
        MediaItem.FLAG_PLAYABLE,
    )

    private fun tile(d: Design, s: EmblemState): MediaItem {
        val desc = MediaDescriptionCompat.Builder()
            .setMediaId(id(d))
            .setTitle(d.title)
            .setSubtitle(if (s.showing(d)) "Showing" else null)
            .setIconUri(artFor(d))
            .build()
        return MediaItem(desc, MediaItem.FLAG_PLAYABLE)
    }

    private fun artFor(d: Design): Uri? {
        if (d.mode == EmblemState.MODE_IMAGE) return art[id(d)]
        val res = DESIGN_ICONS[d.mode] ?: return null
        return Art.resource(this, res, "design_${d.mode}")
    }

    // ---- "playing" = showing on the emblem

    private fun playback(state: Int, error: String? = null): PlaybackStateCompat {
        val b = PlaybackStateCompat.Builder()
            .setActions(
                PlaybackStateCompat.ACTION_PLAY or PlaybackStateCompat.ACTION_PAUSE or
                    PlaybackStateCompat.ACTION_PLAY_FROM_MEDIA_ID or PlaybackStateCompat.ACTION_SKIP_TO_NEXT,
            )
            .addCustomAction(
                PlaybackStateCompat.CustomAction.Builder(ACTION_DIMMER, "Dimmer", R.drawable.ic_dimmer).build(),
            )
            .addCustomAction(
                PlaybackStateCompat.CustomAction.Builder(ACTION_BRIGHTER, "Brighter", R.drawable.ic_brightness).build(),
            )
            .setState(state, PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN, 1f)
        if (error != null) b.setErrorMessage(PlaybackStateCompat.ERROR_CODE_APP_ERROR, error)
        return b.build()
    }

    private fun nowShowing(d: Design) {
        val meta = MediaMetadataCompat.Builder()
            .putString(MediaMetadataCompat.METADATA_KEY_MEDIA_ID, id(d))
            .putString(MediaMetadataCompat.METADATA_KEY_TITLE, d.title)
            .putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_TITLE, d.title)
            .putString(MediaMetadataCompat.METADATA_KEY_ARTIST, "Emblem")
            .putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_SUBTITLE, "Showing on the emblem")
        artFor(d)?.let {
            meta.putString(MediaMetadataCompat.METADATA_KEY_ALBUM_ART_URI, it.toString())
            meta.putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON_URI, it.toString())
        }
        session.setMetadata(meta.build())
        session.setPlaybackState(playback(PlaybackStateCompat.STATE_PLAYING))
    }

    private fun failed() = session.setPlaybackState(
        playback(PlaybackStateCompat.STATE_ERROR, "Couldn't reach the emblem. Is your hotspot on?"),
    )

    private fun shownDesign(s: EmblemState): Design? = s.designs().firstOrNull { s.showing(it) }

    private fun refreshTabs() {
        notifyChildrenChanged(TAB_DESIGNS)
        notifyChildrenChanged(TAB_PICTURES)
    }

    private inner class Callback : MediaSessionCompat.Callback() {
        override fun onPlayFromMediaId(mediaId: String, extras: Bundle?) {
            if (mediaId == RETRY) {
                refreshTabs()
                return
            }
            val d = last?.designs()?.firstOrNull { id(it) == mediaId } ?: parse(mediaId) ?: return
            nowShowing(d)
            io.execute {
                try {
                    last = Emblem.show(d)
                    refreshTabs()
                } catch (e: Exception) {
                    failed()
                }
            }
        }

        override fun onSkipToNext() {
            io.execute {
                try {
                    val s = Emblem.next().also { last = it }
                    shownDesign(s)?.let { nowShowing(it) }
                    refreshTabs()
                } catch (e: Exception) {
                    failed()
                }
            }
        }

        override fun onPlay() {
            io.execute {
                try {
                    val s = connect()
                    shownDesign(s)?.let { nowShowing(it) }
                } catch (e: Exception) {
                    failed()
                }
            }
        }

        override fun onPause() {
            session.setPlaybackState(playback(PlaybackStateCompat.STATE_PAUSED))
        }

        override fun onCustomAction(action: String, extras: Bundle?) {
            io.execute {
                try {
                    val now = (last ?: connect()).brightness
                    val pct = if (action == ACTION_BRIGHTER) minOf(100, now + 20) else maxOf(5, now - 20)
                    last = Emblem.setBrightness(pct)
                } catch (e: Exception) {
                    failed()
                }
            }
        }
    }

    companion object {
        private const val ROOT = "root"
        private const val TAB_DESIGNS = "designs"
        private const val TAB_PICTURES = "pictures"
        private const val RETRY = "retry"
        private const val ACTION_BRIGHTER = "brighter"
        private const val ACTION_DIMMER = "dimmer"

        // Android Auto's content style hints: show tabs and items as grids.
        private const val CONTENT_STYLE_SUPPORTED = "android.media.browse.CONTENT_STYLE_SUPPORTED"
        private const val CONTENT_STYLE_BROWSABLE = "android.media.browse.CONTENT_STYLE_BROWSABLE_HINT"
        private const val CONTENT_STYLE_PLAYABLE = "android.media.browse.CONTENT_STYLE_PLAYABLE_HINT"
        private const val STYLE_GRID = 2

        private val DESIGN_ICONS = mapOf(
            0 to R.drawable.design_roundel, 1 to R.drawable.design_spin, 2 to R.drawable.design_stripes,
            4 to R.drawable.design_text, 5 to R.drawable.design_m50, 6 to R.drawable.design_m50spin,
        )

        fun id(d: Design) = "show:${d.mode}:${d.slot}"

        fun parse(mediaId: String): Design? {
            val p = mediaId.split(':')
            if (p.size != 3 || p[0] != "show") return null
            val mode = p[1].toIntOrNull() ?: return null
            val slot = p[2].toIntOrNull() ?: return null
            val title = EmblemState.BUILT_IN.firstOrNull { it.mode == mode }?.title
                ?: ((if (mode == EmblemState.MODE_IMAGE) "Picture " else "Design ") + (slot + 1))
            return Design(mode, slot, title)
        }
    }
}
