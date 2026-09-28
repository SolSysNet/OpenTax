package org.opentax.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import org.opentax.app.engine.ReturnDocument
import org.opentax.app.engine.Schema
import java.text.DateFormat
import java.util.Date

@Composable
fun ReturnsScreen(vm: AppViewModel, onImport: () -> Unit) {
    var creating by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<SavedReturn?>(null) }

    Text("OpenTax", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary)
    Text("Free, open source federal tax preparation for 2025 and 2026.", style = MaterialTheme.typography.titleMedium)
    Spacer(Modifier.height(4.dp))
    Muted("Your returns stay on this phone. OpenTax has no network access at all, and every number shows how it was figured.")
    Spacer(Modifier.height(16.dp))
    Row {
        Button(onClick = { creating = true }, modifier = Modifier.weight(1f)) { Text("Start a return") }
        Spacer(Modifier.width(12.dp))
        OutlinedButton(onClick = onImport, modifier = Modifier.weight(1f)) { Text("Import a file") }
    }
    Spacer(Modifier.height(16.dp))
    if (vm.saved.isNotEmpty()) {
        Text("YOUR RETURNS", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        for (s in vm.saved) {
            SectionCard {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f).clickable { vm.requestOpen(s.file) }) {
                        Text(s.name, fontWeight = FontWeight.SemiBold)
                        Muted((if (s.encrypted) "Encrypted · " else "") + "Changed " +
                            DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(s.modified)))
                    }
                    TextButton(onClick = { deleting = s }) { Text("Delete", color = negativeColor) }
                }
            }
        }
    }
    Spacer(Modifier.height(24.dp))

    if (creating) NewReturnDialog(vm) { creating = false }
    deleting?.let { s ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete ${s.name}?") },
            text = { Text("The return and its backup are permanently removed from this phone.") },
            confirmButton = { TextButton(onClick = { vm.deleteReturn(s.file); deleting = null }) { Text("Delete", color = negativeColor) } },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun NewReturnDialog(vm: AppViewModel, onDismiss: () -> Unit) {
    var year by remember { mutableIntStateOf((vm.currentYear - 1).coerceIn(2025, 2026)) }
    var first by remember { mutableStateOf("") }
    var last by remember { mutableStateOf("") }
    var status by remember { mutableStateOf("single") }
    var protect by remember { mutableStateOf(false) }
    var password by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }
    var error by remember { mutableStateOf<String?>(null) }
    var statusMenu by remember { mutableStateOf(false) }
    val statuses = Schema.instance.section("info").field("status")!!.choices

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Start a return") },
        text = {
            Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Tax year", modifier = Modifier.padding(end = 8.dp))
                    for (y in listOf(2025, 2026)) {
                        RadioButton(selected = year == y, onClick = { year = y })
                        Text("$y")
                    }
                }
                OutlinedTextField(first, { first = it }, label = { Text("First name") }, singleLine = true)
                OutlinedTextField(last, { last = it }, label = { Text("Last name") }, singleLine = true)
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = { statusMenu = true }, modifier = Modifier.fillMaxWidth()) {
                    Text(statuses.first { it.key == status }.label)
                }
                DropdownMenu(expanded = statusMenu, onDismissRequest = { statusMenu = false }) {
                    for (c in statuses) DropdownMenuItem(text = { Text(c.label) }, onClick = { status = c.key; statusMenu = false })
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = protect, onCheckedChange = { protect = it })
                    Text("Protect with a password")
                }
                if (protect) {
                    PasswordField(password, { password = it }, "Password (at least 8 characters)")
                    PasswordField(confirm, { confirm = it }, "Confirm password")
                    Muted("There's no way to recover a forgotten password.")
                }
                error?.let { Text(it, color = negativeColor) }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                error = if (protect) ReturnDocument.passwordProblem(password) ?: if (password != confirm) "The passwords don't match." else null else null
                if (error == null) {
                    vm.createReturn(year, first, last, status, if (protect) password else null)
                    onDismiss()
                }
            }) { Text("Start") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun PasswordField(value: String, onChange: (String) -> Unit, label: String, isError: Boolean = false) {
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        label = { Text(label) },
        singleLine = true,
        isError = isError,
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, autoCorrectEnabled = false),
        modifier = Modifier.fillMaxWidth(),
    )
}
