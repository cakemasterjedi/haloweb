package com.emblem.auto

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlin.concurrent.thread

/** Phone screen: finds the emblem and explains how to get the app in the car. */
class MainActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var status: TextView
    private lateinit var addressField: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Emblem.init(this)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad * 2, pad, pad)
        }
        fun text(s: String, size: Float = 15f, bold: Boolean = false) = TextView(this).apply {
            this.text = s
            textSize = size
            if (bold) setTypeface(typeface, Typeface.BOLD)
            setPadding(0, pad / 2, 0, pad / 2)
            col.addView(this)
        }
        fun button(label: String, onClick: () -> Unit) = Button(this).apply {
            text = label
            setOnClickListener { onClick() }
            col.addView(this, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        }

        text("Emblem for Android Auto", 22f, true)
        status = text("Looking for the emblem…", 16f)
        button("Find the emblem again") { find() }
        addressField = EditText(this).apply {
            hint = "Emblem address, e.g. 10.212.34.156"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(Emblem.address ?: "")
            col.addView(this, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        }
        button("Use this address") { useAddress(addressField.text.toString().trim()) }
        button("Open the emblem's web page") {
            Emblem.address?.let { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("http://$it/"))) }
        }
        text("Getting it on the car's screen", 18f, true)
        text(
            "1. Open Android Auto's settings (Settings → Connected devices → Connection preferences → " +
                "Android Auto), scroll to the bottom and tap \"Version\" 10 times; allow developer settings.\n" +
                "2. In Android Auto's ⋮ menu → Developer settings, turn on \"Unknown sources\".\n" +
                "3. Back in Android Auto → Customise launcher, make sure Emblem is ticked.\n" +
                "4. Turn on your hotspot so the emblem joins it, then start the car. Emblem appears in the " +
                "Android Auto app list.",
        )
        text(
            "The app finds the emblem on your hotspot by itself. If it doesn't, type the address shown " +
                "in your browser when you open the emblem's page.",
            13f,
        ).setTextColor(Color.GRAY)

        setContentView(ScrollView(this).apply { addView(col) })
        find()
    }

    private fun find() {
        status.text = "Looking for the emblem…"
        thread {
            val host = Emblem.find()
            main.post {
                if (host != null) {
                    status.text = "✓ Emblem found at $host"
                    addressField.setText(host)
                } else {
                    status.text = "Emblem not found. Turn on your hotspot and the emblem, wait a minute for it to join, then tap Find."
                }
            }
        }
    }

    private fun useAddress(host: String) {
        if (host.isEmpty()) return
        status.text = "Checking $host…"
        thread {
            val ok = Emblem.probe(host, 3000)
            if (ok) Emblem.remember(host)
            main.post { status.text = if (ok) "✓ Emblem found at $host" else "No emblem answered at $host" }
        }
    }
}
