package org.opentax.app.ui

import android.app.Application
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.opentax.app.engine.CalcResult
import org.opentax.app.engine.OpenTaxException
import org.opentax.app.engine.PasswordRequiredException
import org.opentax.app.engine.ReturnDocument
import org.opentax.app.engine.WrongPasswordException
import java.io.File
import java.time.LocalDate

sealed interface Screen {
    data object Returns : Screen
    data object Home : Screen
    data object AboutYou : Screen
    data object Dependents : Screen
    data object Income : Screen
    data class IncomeList(val section: String) : Screen
    data class Editor(val section: String, val index: Int) : Screen
    data object Deductions : Screen
    data object Credits : Screen
    data object Payments : Screen
    data object Review : Screen
    data object Forms : Screen
    data class Form(val id: String) : Screen
}

data class SavedReturn(val file: File, val name: String, val modified: Long, val encrypted: Boolean)

/** Something waiting for its password: a saved return, or a file being imported. */
sealed interface UnlockTarget {
    val name: String

    data class Saved(val file: File) : UnlockTarget {
        override val name: String get() = file.nameWithoutExtension
    }

    class Import(val data: ByteArray, val file: File) : UnlockTarget {
        override val name: String get() = file.nameWithoutExtension
    }
}

enum class ExportKind { RETURN, PDF, CSV }

/**
 * App state. Screens edit the open return through [set]; every change recalculates at once
 * (a fraction of a millisecond) and is saved shortly after, and again when the app goes to
 * the background, so work is never lost.
 */
class AppViewModel(app: Application) : AndroidViewModel(app) {

    private val dir = File(app.filesDir, "returns").apply { mkdirs() }

    var stack by mutableStateOf(listOf<Screen>(Screen.Returns)); private set
    val screen: Screen get() = stack.last()

    var doc: ReturnDocument? by mutableStateOf(null); private set
    var result: CalcResult? by mutableStateOf(null); private set
    /** Bumps on every change, so screens re-read values. */
    var revision by mutableIntStateOf(0); private set
    /** Bumps when records are added, removed or reloaded, so text fields reset. */
    var reload by mutableIntStateOf(0); private set

    var saved: List<SavedReturn> by mutableStateOf(emptyList()); private set
    var busy: String? by mutableStateOf(null); private set
    var message: String? by mutableStateOf(null)
    var unlock: UnlockTarget? by mutableStateOf(null); private set
    var unlockError: String? by mutableStateOf(null); private set

    private var saveJob: Job? = null

    init {
        refreshSaved()
    }

    // ------------------------------------------------------------ navigation

    fun open(screen: Screen) {
        stack = stack + screen
    }

    /** Goes to one of the interview steps (from anywhere in the return). */
    fun step(screen: Screen) {
        stack = if (screen == Screen.Home) listOf(Screen.Returns, Screen.Home) else listOf(Screen.Returns, Screen.Home, screen)
    }

    /** Returns false when there's nothing to go back to. */
    fun back(): Boolean {
        if (stack.size <= 1) return false
        if (stack.size == 2 && doc != null) {
            closeReturn()
            return true
        }
        stack = stack.dropLast(1)
        return true
    }

    // ------------------------------------------------------------ saved returns

    fun refreshSaved() {
        saved = dir.listFiles { f -> f.isFile && f.name.endsWith(".otx") }.orEmpty()
            .map { SavedReturn(it, it.nameWithoutExtension, it.lastModified(), runCatching { ReturnDocument.isEncrypted(it) }.getOrDefault(false)) }
            .sortedByDescending { it.modified }
    }

    private fun fileFor(name: String): File {
        val base = name.filter { it.isLetterOrDigit() || it in " -_." }.trim().trim('.').ifEmpty { "Tax Return" }.take(80)
        var file = File(dir, "$base.otx")
        var n = 2
        while (file.exists()) file = File(dir, "$base ($n).otx").also { n++ }
        return file
    }

    fun defaultName(year: Int, first: String, last: String): String =
        "$year Tax Return" + listOf(first, last).joinToString(" ").trim().let { if (it.isEmpty()) "" else " - $it" }

    fun createReturn(year: Int, first: String, last: String, status: String, password: String?) {
        val file = fileFor(defaultName(year, first, last))
        val created = try {
            ReturnDocument.create(file, year, first.trim(), last.trim(), status)
        } catch (e: OpenTaxException) {
            message = e.message
            return
        }
        runBusy(if (password != null) "Protecting your return…" else null, work = {
            if (password != null) created.setPassword(password)
            created.save()
        }, done = {
            show(created)
            step(Screen.AboutYou)
        }, failed = { created.close() })
    }

    fun requestOpen(file: File) {
        if (runCatching { ReturnDocument.isEncrypted(file) }.getOrDefault(false)) {
            unlockError = null
            unlock = UnlockTarget.Saved(file)
        } else {
            runBusy(null, work = { ReturnDocument.open(file, null) }, done = { show(it) })
        }
    }

    /** Imports a return file picked from outside the app, copying it into app storage. */
    fun importData(data: ByteArray, name: String) {
        val file = fileFor(name.removeSuffix(".otx"))
        if (ReturnDocument.isEncrypted(data)) {
            unlockError = null
            unlock = UnlockTarget.Import(data, file)
            return
        }
        runBusy(null, work = { ReturnDocument.fromData(data, null, file).also { it.save() } }, done = {
            show(it)
            message = "Imported ${file.nameWithoutExtension}"
        })
    }

    fun submitPassword(password: String) {
        val target = unlock ?: return
        busy = "Unlocking…"
        viewModelScope.launch {
            try {
                val opened = withContext(Dispatchers.Default) {
                    when (target) {
                        is UnlockTarget.Saved -> ReturnDocument.open(target.file, password)
                        is UnlockTarget.Import -> ReturnDocument.fromData(target.data, password, target.file).also { it.save() }
                    }
                }
                unlock = null
                show(opened)
            } catch (e: WrongPasswordException) {
                unlockError = "That password didn't work. Check it and try again."
            } catch (e: PasswordRequiredException) {
                unlockError = "Enter the password."
            } catch (e: OpenTaxException) {
                unlockError = e.message
            } finally {
                busy = null
            }
        }
    }

    fun cancelUnlock() {
        unlock = null
    }

    fun deleteReturn(file: File) {
        if (doc?.file == file) closeReturn()
        listOf(file, File(file.path + ".bak"), File(file.path + ".tmp")).forEach { it.delete() }
        refreshSaved()
    }

    private fun show(opened: ReturnDocument) {
        doc?.close()
        doc = opened
        result = opened.calculate()
        revision++
        reload++
        stack = listOf(Screen.Returns, Screen.Home)
        refreshSaved()
    }

    fun closeReturn() {
        saveNow()
        doc?.close()
        doc = null
        result = null
        stack = listOf(Screen.Returns)
        refreshSaved()
    }

    // ------------------------------------------------------------ editing

    /** Sets a field. Returns null, or why the text isn't valid (then nothing changed). */
    fun set(section: String, key: String, value: String, index: Int = 0): String? {
        val d = doc ?: return null
        val problem = d.set(section, key, value, index)
        if (problem == null) changed()
        return problem
    }

    fun value(section: String, key: String, index: Int = 0): String = doc?.value(section, key, index).orEmpty()

    fun add(section: String): Int {
        val index = doc?.add(section) ?: return -1
        changed()
        reload++
        return index
    }

    fun remove(section: String, index: Int) {
        doc?.remove(section, index)
        changed()
        reload++
    }

    private fun changed() {
        val d = doc ?: return
        result = d.calculate()
        revision++
        saveJob?.cancel()
        saveJob = viewModelScope.launch {
            delay(600)
            saveNow()
        }
    }

    fun saveNow() {
        saveJob?.cancel()
        try {
            doc?.save()
        } catch (e: OpenTaxException) {
            message = "Couldn't save: ${e.message}"
        }
    }

    // ------------------------------------------------------------ passwords and exports

    fun setPassword(password: String?) {
        val d = doc ?: return
        runBusy(if (password != null) "Protecting your return…" else null, work = {
            d.setPassword(password)
            d.save()
        }, done = {
            revision++
            refreshSaved()
            message = if (password != null) "Your return is now encrypted" else "Password removed"
        })
    }

    fun export(kind: ExportKind, uri: Uri) {
        val d = doc ?: return
        val bytes = when (kind) {
            ExportKind.RETURN -> d.exportData()
            ExportKind.PDF -> d.pdf()
            ExportKind.CSV -> d.csv().toByteArray()
        }
        try {
            getApplication<Application>().contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) }
            message = when {
                kind == ExportKind.RETURN && d.hasPassword -> "Saved (encrypted)"
                kind != ExportKind.RETURN && d.hasPassword -> "Saved. Note: this file isn't encrypted."
                else -> "Saved"
            }
        } catch (e: Exception) {
            message = "Couldn't save the file: ${e.message}"
        }
    }

    // ------------------------------------------------------------ helpers

    /** Runs [work] off the main thread (key derivation can take a second), then [done] on it. */
    private fun <T> runBusy(label: String?, work: () -> T, done: (T) -> Unit, failed: () -> Unit = {}) {
        busy = label ?: "Opening…"
        viewModelScope.launch {
            try {
                val value = withContext(Dispatchers.Default) { work() }
                done(value)
            } catch (e: OpenTaxException) {
                failed()
                message = e.message
            } finally {
                busy = null
            }
        }
    }

    val currentYear: Int get() = LocalDate.now().year

    override fun onCleared() {
        saveNow()
        doc?.close()
        doc = null
    }
}
