package com.emblem.auto

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.ConnectivityManager
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONObject

/** A design the emblem can show: a built-in mode, or a picture slot (mode 3). */
data class Design(val mode: Int, val slot: Int, val title: String)

/** The parts of the emblem's /api/state the app uses. */
data class EmblemState(
    val showMode: Int,
    val showSlot: Int,
    val slots: List<Int>,  // per slot: 0 empty, 1 picture, 2 animation
    val brightness: Int,
    val cycling: Boolean,
) {
    fun showing(d: Design) = d.mode == showMode && (d.mode != MODE_IMAGE || d.slot == showSlot)

    /** The built-in designs, then every filled picture slot. */
    fun designs(): List<Design> = BUILT_IN + slots.withIndex().filter { it.value != 0 }.map { (i, kind) ->
        Design(MODE_IMAGE, i, (if (kind == 2) "Animation " else "Picture ") + (i + 1))
    }

    companion object {
        const val MODE_IMAGE = 3
        val BUILT_IN = listOf(
            Design(0, 0, "Roundel"), Design(1, 0, "Spin"), Design(2, 0, "Stripes"),
            Design(4, 0, "Text"), Design(5, 0, "M 50"), Design(6, 0, "M 50 Spin"),
        )

        fun parse(json: String): EmblemState {
            val o = JSONObject(json)
            val a = o.getJSONArray("slots")
            return EmblemState(
                showMode = o.optInt("showMode", o.optInt("mode")),
                showSlot = o.optInt("showSlot", o.optInt("imageSlot")),
                slots = List(a.length()) { a.getInt(it) },
                brightness = o.optInt("brightness", 80),
                cycling = o.optBoolean("cycling"),
            )
        }
    }
}

/**
 * Talks to the emblem over Wi-Fi with the same requests as its web page.
 * The emblem is either on the phone's hotspot (any address) or the phone is
 * on the emblem's own network (192.168.4.1).
 */
object Emblem {
    private const val PREFS = "emblem"
    @Volatile var address: String? = null
        private set
    private lateinit var app: Context

    fun init(context: Context) {
        if (::app.isInitialized) return
        app = context.applicationContext
        address = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("address", null)
    }

    fun remember(host: String) {
        address = host
        app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("address", host).apply()
    }

    // ---- requests

    /** Opens host+path over the network that reaches host (Wi-Fi without
     *  internet isn't the default network, so ask for it explicitly). */
    private fun open(host: String, path: String, connectMs: Int, readMs: Int): HttpURLConnection {
        val url = URL("http://$host$path")
        var conn: HttpURLConnection? = null
        try {
            val target = InetAddress.getByName(host)
            val cm = app.getSystemService(ConnectivityManager::class.java)
            @Suppress("DEPRECATION")
            for (n in cm.allNetworks) {
                val lp = cm.getLinkProperties(n) ?: continue
                if (lp.linkAddresses.any { la -> sameSubnet(la.address, target, la.prefixLength) }) {
                    conn = n.openConnection(url) as HttpURLConnection
                    break
                }
            }
        } catch (_: Exception) {
        }
        val c = conn ?: url.openConnection() as HttpURLConnection
        c.connectTimeout = connectMs
        c.readTimeout = readMs
        c.useCaches = false
        return c
    }

    private fun sameSubnet(a: InetAddress, b: InetAddress, prefix: Int): Boolean {
        if (a !is Inet4Address || b !is Inet4Address) return false
        val bits = prefix.coerceIn(0, 32)
        if (bits == 0) return false
        val mask = if (bits == 32) -1 else (-1 shl (32 - bits))
        return (toInt(a) and mask) == (toInt(b) and mask)
    }

    private fun toInt(a: Inet4Address) = a.address.fold(0) { acc, x -> (acc shl 8) or (x.toInt() and 0xFF) }

    private fun readAll(c: HttpURLConnection): ByteArray {
        val code = c.responseCode
        val stream = if (code in 200..299) c.inputStream else c.errorStream
        val bytes = stream?.use { s ->
            val out = ByteArrayOutputStream()
            s.copyTo(out)
            out.toByteArray()
        } ?: ByteArray(0)
        if (code !in 200..299) throw java.io.IOException("Emblem said $code: ${String(bytes).take(120)}")
        return bytes
    }

    private fun get(host: String, path: String, connectMs: Int = 3000, readMs: Int = 6000): ByteArray {
        val c = open(host, path, connectMs, readMs)
        try {
            return readAll(c)
        } finally {
            c.disconnect()
        }
    }

    private fun post(path: String, fields: Map<String, Any>): String {
        val host = address ?: throw java.io.IOException("Emblem not found")
        val body = fields.entries.joinToString("&") { (k, v) ->
            URLEncoder.encode(k, "UTF-8") + "=" + URLEncoder.encode(v.toString(), "UTF-8")
        }.toByteArray()
        val c = open(host, path, 3000, 8000)
        try {
            c.requestMethod = "POST"
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            c.setFixedLengthStreamingMode(body.size)
            c.outputStream.use { it.write(body) }
            return String(readAll(c))
        } finally {
            c.disconnect()
        }
    }

    fun state(): EmblemState {
        val host = address ?: throw java.io.IOException("Emblem not found")
        return EmblemState.parse(String(get(host, "/api/state")))
    }

    /** Shows a design; returns the new state. */
    fun show(d: Design): EmblemState {
        val fields = mutableMapOf<String, Any>("mode" to d.mode)
        if (d.mode == EmblemState.MODE_IMAGE) fields["imageSlot"] = d.slot
        return EmblemState.parse(post("/api/set", fields))
    }

    fun next(): EmblemState = EmblemState.parse(post("/api/next", emptyMap()))

    fun setBrightness(pct: Int): EmblemState = EmblemState.parse(post("/api/set", mapOf("brightness" to pct)))

    /** Small picture of a slot (first frame of an animation), or null. */
    fun thumb(slot: Int): Bitmap? = try {
        val host = address ?: return null
        val bytes = get(host, "/api/thumb?slot=$slot", 3000, 8000)
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
    } catch (_: Exception) {
        null
    }

    // ---- finding the emblem

    /** True if host answers like an emblem. */
    fun probe(host: String, connectMs: Int = 1500): Boolean = try {
        val s = String(get(host, "/api/state", connectMs, 2500))
        s.contains("\"slots\"") && s.contains("\"showMode\"")
    } catch (_: Exception) {
        false
    }

    /**
     * Finds the emblem: the last address that worked, its own network
     * (192.168.4.1), then every address on the phone's local networks
     * (the hotspot). Returns the address, or null.
     */
    fun find(): String? {
        address?.let { if (probe(it)) return it }
        if (probe("192.168.4.1")) {
            remember("192.168.4.1")
            return address
        }
        val candidates = LinkedHashSet<String>()
        try {
            for (ni in NetworkInterface.getNetworkInterfaces()) {
                if (!ni.isUp || ni.isLoopback) continue
                for (ia in ni.interfaceAddresses) {
                    val a = ia.address as? Inet4Address ?: continue
                    if (!a.isSiteLocalAddress) continue
                    val own = a.address
                    // The /24 around the phone's address (a hotspot is usually exactly that).
                    for (i in 1..254) {
                        if (i == (own[3].toInt() and 0xFF)) continue
                        candidates += "${own[0].toInt() and 0xFF}.${own[1].toInt() and 0xFF}.${own[2].toInt() and 0xFF}.$i"
                    }
                }
            }
        } catch (_: Exception) {
        }
        if (candidates.isEmpty()) return null
        val found = AtomicReference<String?>(null)
        val pool = Executors.newFixedThreadPool(48)
        for (host in candidates) {
            pool.execute {
                if (found.get() == null && probe(host, 700)) found.compareAndSet(null, host)
            }
        }
        pool.shutdown()
        val deadline = System.currentTimeMillis() + 20000
        while (!pool.isTerminated && found.get() == null && System.currentTimeMillis() < deadline) {
            pool.awaitTermination(200, TimeUnit.MILLISECONDS)
        }
        pool.shutdownNow()
        return found.get()?.also { remember(it) }
    }
}
