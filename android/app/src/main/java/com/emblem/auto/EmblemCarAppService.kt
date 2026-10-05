package com.emblem.auto

import android.content.Intent
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import androidx.car.app.CarAppService
import androidx.car.app.CarContext
import androidx.car.app.CarToast
import androidx.car.app.Screen
import androidx.car.app.Session
import androidx.car.app.constraints.ConstraintManager
import androidx.car.app.model.Action
import androidx.car.app.model.ActionStrip
import androidx.car.app.model.CarIcon
import androidx.car.app.model.GridItem
import androidx.car.app.model.GridTemplate
import androidx.car.app.model.ItemList
import androidx.car.app.model.MessageTemplate
import androidx.car.app.model.Template
import androidx.car.app.validation.HostValidator
import androidx.core.graphics.drawable.IconCompat
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import java.util.concurrent.Executors

/** Android Auto entry point: a grid of the emblem's designs. */
class EmblemCarAppService : CarAppService() {
    // Installed outside the Play Store for one person's car: accept any
    // Android Auto host (the usual check only knows Google-signed hosts).
    override fun createHostValidator(): HostValidator = HostValidator.ALLOW_ALL_HOSTS_VALIDATOR

    override fun onCreateSession(): Session = object : Session() {
        override fun onCreateScreen(intent: Intent): Screen {
            Emblem.init(carContext)
            return DesignsScreen(carContext)
        }
    }
}

private val io = Executors.newSingleThreadExecutor()
private val main = Handler(Looper.getMainLooper())

private fun icon(ctx: CarContext, res: Int) = CarIcon.Builder(IconCompat.createWithResource(ctx, res)).build()

private val DESIGN_ICONS = mapOf(
    0 to R.drawable.design_roundel, 1 to R.drawable.design_spin, 2 to R.drawable.design_stripes,
    4 to R.drawable.design_text, 5 to R.drawable.design_m50, 6 to R.drawable.design_m50spin,
)

/** The designs, as a grid; tap one to show it. */
class DesignsScreen(ctx: CarContext) : Screen(ctx) {
    private var state: EmblemState? = null
    private var error: String? = null
    private var busy = true
    private val thumbs = HashMap<String, Bitmap>()  // "slot:kind" -> picture

    init {
        lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStart(owner: LifecycleOwner) = refresh()
        })
    }

    private fun refresh() {
        io.execute {
            try {
                if (Emblem.address == null || !Emblem.probe(Emblem.address!!)) {
                    Emblem.find() ?: throw java.io.IOException(
                        "Emblem not found. Check the emblem is on and has joined your phone's hotspot " +
                            "(or that the phone is on the emblem's Wi-Fi).",
                    )
                }
                val s = Emblem.state()
                loadThumbs(s)
                main.post { state = s; error = null; busy = false; invalidate() }
            } catch (e: Exception) {
                main.post { error = e.message ?: "Couldn't reach the emblem"; busy = false; invalidate() }
            }
        }
    }

    // Runs on io.
    private fun loadThumbs(s: EmblemState) {
        for ((slot, kind) in s.slots.withIndex()) {
            if (kind == 0) continue
            val key = "$slot:$kind"
            if (thumbs.containsKey(key)) continue
            Emblem.thumb(slot)?.let { bmp -> main.post { thumbs[key] = bmp; invalidate() } }
        }
    }

    private fun choose(d: Design) {
        // Show the choice straight away; the emblem crossfades to it.
        state = state?.copy(showMode = d.mode, showSlot = if (d.mode == EmblemState.MODE_IMAGE) d.slot else state!!.showSlot)
        invalidate()
        io.execute {
            try {
                val s = Emblem.show(d)
                main.post { state = s; invalidate() }
            } catch (e: Exception) {
                main.post { CarToast.makeText(carContext, "Couldn't reach the emblem", CarToast.LENGTH_SHORT).show(); refresh() }
            }
        }
    }

    private fun next() {
        io.execute {
            try {
                val s = Emblem.next()
                main.post { state = s; invalidate() }
            } catch (e: Exception) {
                main.post { CarToast.makeText(carContext, "Couldn't reach the emblem", CarToast.LENGTH_SHORT).show() }
            }
        }
    }

    override fun onGetTemplate(): Template {
        val err = error
        val s = state
        if (s == null && err != null) {
            return MessageTemplate.Builder(err)
                .setTitle("Emblem")
                .setHeaderAction(Action.APP_ICON)
                .addAction(
                    Action.Builder().setTitle("Try again").setOnClickListener {
                        error = null; busy = true; invalidate(); refresh()
                    }.build(),
                )
                .build()
        }
        if (s == null) {
            return GridTemplate.Builder().setTitle("Emblem").setHeaderAction(Action.APP_ICON).setLoading(true).build()
        }

        var limit = 100
        try {
            limit = carContext.getCarService(ConstraintManager::class.java)
                .getContentLimit(ConstraintManager.CONTENT_LIMIT_TYPE_GRID)
        } catch (_: Exception) {
        }
        val list = ItemList.Builder()
        for (d in s.designs().take(limit)) {
            val img = if (d.mode == EmblemState.MODE_IMAGE) {
                thumbs["${d.slot}:${s.slots[d.slot]}"]?.let { CarIcon.Builder(IconCompat.createWithBitmap(it)).build() }
                    ?: icon(carContext, R.drawable.ic_picture)
            } else {
                icon(carContext, DESIGN_ICONS[d.mode] ?: R.drawable.ic_picture)
            }
            val item = GridItem.Builder()
                .setTitle(d.title)
                .setImage(img, GridItem.IMAGE_TYPE_LARGE)
                .setOnClickListener { choose(d) }
            if (s.showing(d)) item.setText(if (s.cycling) "Showing · auto-cycle" else "Showing")
            list.addItem(item.build())
        }
        val strip = ActionStrip.Builder()
            .addAction(
                Action.Builder().setTitle("Next").setIcon(icon(carContext, R.drawable.ic_next))
                    .setOnClickListener { next() }.build(),
            )
            .addAction(
                Action.Builder().setIcon(icon(carContext, R.drawable.ic_brightness))
                    .setOnClickListener { screenManager.push(BrightnessScreen(carContext, s.brightness)) }.build(),
            )
            .build()
        return GridTemplate.Builder()
            .setTitle("Emblem")
            .setHeaderAction(Action.APP_ICON)
            .setSingleList(list.build())
            .setActionStrip(strip)
            .build()
    }
}

/** Brightness steps. */
class BrightnessScreen(ctx: CarContext, private val current: Int) : Screen(ctx) {
    override fun onGetTemplate(): Template {
        val list = ItemList.Builder()
        for (pct in listOf(20, 40, 60, 80, 100)) {
            val item = GridItem.Builder()
                .setTitle("$pct%")
                .setImage(icon(carContext, R.drawable.ic_brightness), GridItem.IMAGE_TYPE_ICON)
                .setOnClickListener {
                    io.execute {
                        try {
                            Emblem.setBrightness(pct)
                        } catch (_: Exception) {
                            main.post { CarToast.makeText(carContext, "Couldn't reach the emblem", CarToast.LENGTH_SHORT).show() }
                        }
                    }
                    screenManager.pop()
                }
            if (kotlin.math.abs(pct - current) < 10) item.setText("Now")
            list.addItem(item.build())
        }
        return GridTemplate.Builder()
            .setTitle("Brightness")
            .setHeaderAction(Action.BACK)
            .setSingleList(list.build())
            .build()
    }
}
