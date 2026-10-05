package com.emblem.auto

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.graphics.Bitmap
import android.net.Uri
import android.os.ParcelFileDescriptor
import java.io.File
import java.io.FileNotFoundException

/**
 * Serves the tile pictures to Android Auto as content:// URIs (it can't
 * read the app's resources or files directly): the design icons, copied
 * out of the app once, and the thumbnails of the picture slots.
 */
class ArtProvider : ContentProvider() {
    override fun onCreate() = true

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        val name = uri.lastPathSegment ?: throw FileNotFoundException()
        val file = File(Art.dir(context!!), name)
        if (name.contains('/') || !file.exists()) throw FileNotFoundException(name)
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
    }

    override fun getType(uri: Uri) = "image/png"
    override fun query(uri: Uri, p: Array<String>?, s: String?, a: Array<String>?, o: String?): Cursor? = null
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, s: String?, a: Array<String>?) = 0
    override fun update(uri: Uri, v: ContentValues?, s: String?, a: Array<String>?) = 0
}

object Art {
    private const val AUTHORITY = "com.emblem.auto.art"

    fun dir(context: Context) = File(context.cacheDir, "art").apply { mkdirs() }

    fun uri(name: String): Uri = Uri.parse("content://$AUTHORITY/$name")

    /** Copies a drawable PNG out for the car (once). Returns its URI. */
    fun resource(context: Context, res: Int, name: String): Uri {
        val f = File(dir(context), "$name.png")
        if (!f.exists()) {
            context.resources.openRawResource(res).use { input -> f.outputStream().use { input.copyTo(it) } }
        }
        return uri(f.name)
    }

    /** Saves a slot's thumbnail; a new name each time so the car doesn't
     *  keep showing an old picture after the slot changes. */
    fun thumbnail(context: Context, slot: Int, bmp: Bitmap): Uri {
        val d = dir(context)
        d.listFiles { f -> f.name.startsWith("slot$slot-") }?.forEach { it.delete() }
        val f = File(d, "slot$slot-${System.currentTimeMillis()}.png")
        f.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return uri(f.name)
    }
}
