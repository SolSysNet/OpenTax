package org.opentax.app.ui

import android.provider.OpenableColumns
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import org.opentax.app.engine.ReturnDocument

private fun titleFor(screen: Screen): String = when (screen) {
    Screen.Returns -> "OpenTax"
    Screen.Home -> "Tax home"
    Screen.AboutYou -> "About you"
    Screen.Dependents -> "Dependents"
    Screen.Income, is Screen.IncomeList -> "Income"
    is Screen.Editor -> "Edit"
    Screen.Deductions -> "Deductions"
    Screen.Credits -> "Credits"
    Screen.Payments -> "Payments"
    Screen.Review -> "Review"
    Screen.Forms, is Screen.Form -> "Forms"
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OpenTaxApp(vm: AppViewModel = viewModel()) {
    val context = LocalContext.current
    val snackbar = remember { SnackbarHostState() }
    var menu by remember { mutableStateOf(false) }
    var passwordDialog by remember { mutableStateOf(false) }
    var confirmRemovePassword by remember { mutableStateOf(false) }
    var pendingExport by remember { mutableStateOf(ExportKind.PDF) }

    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        val resolver = context.contentResolver
        val name = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        } ?: "Imported return"
        val data = resolver.openInputStream(uri)?.use { it.readBytes() }
        if (data == null || data.size > 16 * 1024 * 1024) vm.message = "Couldn't read that file." else vm.importData(data, name)
    }
    val exporter = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("*/*")) { uri ->
        if (uri != null) vm.export(pendingExport, uri)
    }
    fun export(kind: ExportKind) {
        val doc = vm.doc ?: return
        pendingExport = kind
        val base = doc.pdfFileName.removeSuffix(".pdf")
        exporter.launch(
            when (kind) {
                ExportKind.PDF -> "$base.pdf"
                ExportKind.CSV -> "$base.csv"
                ExportKind.RETURN -> doc.file.name
            },
        )
    }

    BackHandler(enabled = vm.stack.size > 1) { vm.back() }
    LaunchedEffect(vm.message) {
        vm.message?.let {
            snackbar.showSnackbar(it)
            vm.message = null
        }
    }

    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    title = { Text(titleFor(vm.screen)) },
                    navigationIcon = {
                        if (vm.stack.size > 1) TextButton(onClick = { vm.back() }) { Text("‹ Back") }
                    },
                    actions = {
                        if (vm.doc != null) {
                            if (vm.doc?.hasPassword == true) Text("Encrypted", color = positiveColor, modifier = Modifier.padding(end = 4.dp))
                            TextButton(onClick = { menu = true }) { Text("More") }
                            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                                DropdownMenuItem(text = { Text("Save PDF…") }, onClick = { menu = false; export(ExportKind.PDF) })
                                DropdownMenuItem(text = { Text("Export return file…") }, onClick = { menu = false; export(ExportKind.RETURN) })
                                DropdownMenuItem(text = { Text("Export lines as CSV…") }, onClick = { menu = false; export(ExportKind.CSV) })
                                HorizontalDivider()
                                DropdownMenuItem(
                                    text = { Text(if (vm.doc?.hasPassword == true) "Change password…" else "Protect with password…") },
                                    onClick = { menu = false; passwordDialog = true },
                                )
                                if (vm.doc?.hasPassword == true) {
                                    DropdownMenuItem(text = { Text("Remove password…") }, onClick = { menu = false; confirmRemovePassword = true })
                                }
                                HorizontalDivider()
                                DropdownMenuItem(text = { Text("Close return") }, onClick = { menu = false; vm.closeReturn() })
                            }
                        }
                    },
                )
                vm.result?.let { if (vm.doc != null) RefundBar(it) }
            }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        // Each screen gets its own scroll position.
        key(vm.screen) {
            Column(
                Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 12.dp),
            ) {
                when (val s = vm.screen) {
                    Screen.Returns -> ReturnsScreen(vm) { importer.launch(arrayOf("*/*")) }
                    Screen.Home -> HomeScreen(vm)
                    Screen.AboutYou -> AboutYouScreen(vm)
                    Screen.Dependents -> DependentsScreen(vm)
                    Screen.Income -> IncomeScreen(vm)
                    is Screen.IncomeList -> IncomeListScreen(vm, s.section)
                    is Screen.Editor -> EditorScreen(vm, s.section, s.index)
                    Screen.Deductions -> DeductionsScreen(vm)
                    Screen.Credits -> CreditsScreen(vm)
                    Screen.Payments -> PaymentsScreen(vm)
                    Screen.Review -> ReviewScreen(vm) { export(ExportKind.PDF) }
                    Screen.Forms -> FormsScreen(vm)
                    is Screen.Form -> FormScreen(vm, s.id)
                }
            }
        }
    }

    vm.unlock?.let { target -> UnlockDialog(vm, target) }
    if (passwordDialog) SetPasswordDialog(vm) { passwordDialog = false }
    if (confirmRemovePassword) {
        AlertDialog(
            onDismissRequest = { confirmRemovePassword = false },
            title = { Text("Remove the password?") },
            text = { Text("The return will be saved unencrypted on this phone.") },
            confirmButton = { TextButton(onClick = { confirmRemovePassword = false; vm.setPassword(null) }) { Text("Remove", color = negativeColor) } },
            dismissButton = { TextButton(onClick = { confirmRemovePassword = false }) { Text("Cancel") } },
        )
    }
    vm.busy?.let { label ->
        Dialog(onDismissRequest = {}, properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false)) {
            SectionCard {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator()
                    Box(Modifier.width(16.dp))
                    Text(label)
                }
            }
        }
    }
}

@Composable
private fun UnlockDialog(vm: AppViewModel, target: UnlockTarget) {
    var password by remember(target) { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = { vm.cancelUnlock() },
        title = { Text("Unlock ${target.name}") },
        text = {
            Column {
                Text("This return is protected with a password.")
                PasswordField(password, { password = it }, "Password", isError = vm.unlockError != null)
                vm.unlockError?.let { Text(it, color = negativeColor) }
            }
        },
        confirmButton = { TextButton(onClick = { vm.submitPassword(password); password = "" }) { Text("Unlock") } },
        dismissButton = { TextButton(onClick = { vm.cancelUnlock() }) { Text("Cancel") } },
    )
}

@Composable
private fun SetPasswordDialog(vm: AppViewModel, onDismiss: () -> Unit) {
    var password by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }
    var error by remember { mutableStateOf<String?>(null) }
    val changing = vm.doc?.hasPassword == true
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (changing) "Change the password" else "Protect with a password") },
        text = {
            Column {
                Text("The return is encrypted, so it can't be read without the password. There's no way to recover a forgotten password.")
                PasswordField(password, { password = it }, "New password (at least 8 characters)")
                PasswordField(confirm, { confirm = it }, "Confirm password")
                error?.let { Text(it, color = negativeColor) }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                error = ReturnDocument.passwordProblem(password) ?: if (password != confirm) "The passwords don't match." else null
                if (error == null) {
                    vm.setPassword(password)
                    onDismiss()
                }
            }) { Text(if (changing) "Change" else "Protect") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
