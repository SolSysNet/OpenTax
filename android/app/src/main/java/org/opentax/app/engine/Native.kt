package org.opentax.app.engine

/** An error from the tax engine, with a message meant for people. */
open class OpenTaxException(message: String) : Exception(message)

/** The return is encrypted and no password was given. */
class PasswordRequiredException(message: String) : OpenTaxException(message)

/** The password is wrong, or the file was damaged or altered. */
class WrongPasswordException(message: String) : OpenTaxException(message)

/**
 * The C++ engine (the same code as the desktop app and CLI), through the JNI bridge in
 * src/main/cpp/bridge.cpp. Handles are owned by [ReturnDocument]; use that instead.
 */
internal object Native {
    init {
        System.loadLibrary("opentax_jni")
    }

    external fun schema(): String

    external fun create(year: Int, first: String, last: String, status: String): Long
    external fun free(handle: Long)

    external fun isEncryptedFile(path: String): Boolean
    external fun isEncryptedData(data: ByteArray): Boolean
    external fun load(path: String, password: String?): Long
    external fun loadData(data: ByteArray, password: String?): Long
    external fun save(handle: Long, path: String)
    external fun exportData(handle: Long): ByteArray

    external fun passwordProblem(password: String): String?
    external fun setPassword(handle: Long, password: String?)
    external fun hasPassword(handle: Long): Boolean

    external fun get(handle: Long, section: String, index: Int): String
    external fun set(handle: Long, section: String, index: Int, key: String, value: String): String?
    external fun count(handle: Long, section: String): Int
    external fun add(handle: Long, section: String): Int
    external fun remove(handle: Long, section: String, index: Int)

    external fun calculate(handle: Long): String
    external fun displayName(handle: Long): String
    external fun pdf(handle: Long): ByteArray
    external fun pdfFileName(handle: Long): String
    external fun csv(handle: Long): String
}
