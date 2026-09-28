package org.opentax.app.engine

import java.io.File

/**
 * An open tax return, owned by the native engine. Fields are read and written as text in the
 * same form the file format uses; the engine validates every value.
 *
 * Not thread-safe: use it from one thread at a time.
 */
class ReturnDocument private constructor(private var handle: Long, var file: File) : AutoCloseable {

    private fun h(): Long {
        check(handle != 0L) { "the return is closed" }
        return handle
    }

    /** The record's fields as key -> text. [index] is ignored for single records. */
    fun get(section: String, index: Int = 0): Map<String, String> {
        val json = org.json.JSONObject(Native.get(h(), section, index))
        return json.keys().asSequence().associateWith { json.getString(it) }
    }

    fun value(section: String, key: String, index: Int = 0): String = get(section, index)[key].orEmpty()

    /** Sets a field. Returns null on success, or why the text isn't valid (the value is unchanged). */
    fun set(section: String, key: String, value: String, index: Int = 0): String? = Native.set(h(), section, index, key, value)

    fun count(section: String): Int = Native.count(h(), section)
    fun add(section: String): Int = Native.add(h(), section)
    fun remove(section: String, index: Int) = Native.remove(h(), section, index)

    fun calculate(): CalcResult = CalcResult.parse(Native.calculate(h()))
    val displayName: String get() = Native.displayName(h())
    val year: Int get() = value("info", "year").toIntOrNull() ?: 2025
    val status: String get() = value("info", "status")

    /** Saves atomically to [file] (encrypted when the return has a password). */
    fun save() = Native.save(h(), file.absolutePath)
    fun exportData(): ByteArray = Native.exportData(h())
    fun pdf(): ByteArray = Native.pdf(h())
    val pdfFileName: String get() = Native.pdfFileName(h())
    fun csv(): String = Native.csv(h())

    val hasPassword: Boolean get() = Native.hasPassword(h())

    /** Sets or (with null) removes the password. Deriving the key takes a moment: call off the main thread. */
    fun setPassword(password: String?) = Native.setPassword(h(), password)

    override fun close() {
        if (handle != 0L) Native.free(handle)
        handle = 0
    }

    companion object {
        fun create(file: File, year: Int, first: String, last: String, status: String): ReturnDocument =
            ReturnDocument(Native.create(year, first, last, status), file)

        /** Opens a saved return. Slow for an encrypted one (key derivation): call off the main thread. */
        fun open(file: File, password: String?): ReturnDocument = ReturnDocument(Native.load(file.absolutePath, password), file)

        /** Reads a return from file contents, to be saved as [file]. */
        fun fromData(data: ByteArray, password: String?, file: File): ReturnDocument =
            ReturnDocument(Native.loadData(data, password), file)

        fun isEncrypted(file: File): Boolean = Native.isEncryptedFile(file.absolutePath)
        fun isEncrypted(data: ByteArray): Boolean = Native.isEncryptedData(data)
        fun passwordProblem(password: String): String? = Native.passwordProblem(password)
    }
}
