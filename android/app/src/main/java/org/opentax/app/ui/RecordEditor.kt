package org.opentax.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.DatePicker
import androidx.compose.material3.DatePickerDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberDatePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import org.opentax.app.engine.FieldKind
import org.opentax.app.engine.FieldSpec
import org.opentax.app.engine.Schema
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset

/**
 * Every field of a record, generated from the engine's schema. [hide] skips fields by key.
 * The owner (you/spouse) field only appears on joint returns.
 */
@Composable
fun RecordEditor(vm: AppViewModel, section: String, index: Int = 0, hide: (String) -> Boolean = { false }) {
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision  // re-read values after any change
    val joint = vm.value("info", "status") == "mfj"
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        for (field in Schema.instance.section(section).fields) {
            if (hide(field.key) || (field.key == "owner" && !joint)) continue
            FieldRow(vm, section, index, field)
        }
    }
}

/** One field, by key. */
@Composable
fun Field(vm: AppViewModel, section: String, key: String, index: Int = 0, label: String? = null) {
    val spec = Schema.instance.section(section).field(key) ?: return
    FieldRow(vm, section, index, if (label != null) spec.copy(label = label) else spec)
}

@Composable
private fun FieldRow(vm: AppViewModel, section: String, index: Int, field: FieldSpec) {
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    val stored = vm.value(section, field.key, index)
    when (field.kind) {
        FieldKind.BOOL -> Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).padding(end = 12.dp)) {
                Text(field.label.cleanLabel())
                if (field.help.isNotEmpty()) {
                    Text(field.help, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            Switch(checked = stored == "yes", onCheckedChange = { vm.set(section, field.key, if (it) "yes" else "no", index) })
        }
        FieldKind.CHOICE -> ChoiceField(vm, section, index, field, stored)
        else -> TextEntry(vm, section, index, field, stored)
    }
}

/** Labels carry form box numbers with two spaces ("Box 1  Wages"); one reads better on a phone. */
private fun String.cleanLabel(): String = replace("  ", " · ")

@Composable
private fun ChoiceField(vm: AppViewModel, section: String, index: Int, field: FieldSpec, stored: String) {
    var expanded by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Text(field.label.cleanLabel(), style = MaterialTheme.typography.labelLarge)
        Box {
            OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
                Text(field.choices.firstOrNull { it.key == stored }?.label ?: stored, modifier = Modifier.fillMaxWidth())
            }
            DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                for (choice in field.choices) {
                    DropdownMenuItem(text = { Text(choice.label) }, onClick = {
                        vm.set(section, field.key, choice.key, index)
                        expanded = false
                    })
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TextEntry(vm: AppViewModel, section: String, index: Int, field: FieldSpec, stored: String) {
    // The text being typed is kept here; the engine validates each change and keeps the last
    // valid value. It resets when the record is reloaded (another entry, another return).
    var text by remember(section, index, field.key, vm.reload) { mutableStateOf(stored) }
    var error by remember(section, index, field.key, vm.reload) { mutableStateOf<String?>(null) }
    var showHelp by remember { mutableStateOf(false) }
    var picking by remember { mutableStateOf(false) }

    fun update(value: String) {
        text = value
        val sent = if (field.kind == FieldKind.INT && value.isBlank()) "0" else value
        error = vm.set(section, field.key, sent, index)?.removePrefix("${field.key}: ")
    }

    OutlinedTextField(
        value = text,
        onValueChange = ::update,
        label = { Text(field.label.cleanLabel()) },
        placeholder = when (field.kind) {
            FieldKind.MONEY -> ({ Text("0.00") })
            FieldKind.DATE -> ({ Text("YYYY-MM-DD") })
            else -> null
        },
        isError = error != null,
        supportingText = when {
            error != null -> ({ Text(error!!) })
            showHelp -> ({ Text(field.help) })
            else -> null
        },
        trailingIcon = {
            Row {
                if (field.kind == FieldKind.DATE) TextButton(onClick = { picking = true }) { Text("Pick") }
                if (field.help.isNotEmpty()) TextButton(onClick = { showHelp = !showHelp }) { Text("?") }
            }
        },
        singleLine = true,
        keyboardOptions = KeyboardOptions(
            keyboardType = when (field.kind) {
                FieldKind.MONEY -> KeyboardType.Decimal
                FieldKind.INT -> KeyboardType.Number
                else -> KeyboardType.Text
            },
        ),
        modifier = Modifier.fillMaxWidth(),
    )

    if (picking) {
        val initial = runCatching { LocalDate.parse(text.trim()) }.getOrNull() ?: LocalDate.of(1985, 1, 1)
        val state = rememberDatePickerState(initialSelectedDateMillis = initial.atStartOfDay().toInstant(ZoneOffset.UTC).toEpochMilli())
        DatePickerDialog(
            onDismissRequest = { picking = false },
            confirmButton = {
                TextButton(onClick = {
                    state.selectedDateMillis?.let { update(Instant.ofEpochMilli(it).atZone(ZoneOffset.UTC).toLocalDate().toString()) }
                    picking = false
                }) { Text("OK") }
            },
            dismissButton = { TextButton(onClick = { picking = false }) { Text("Cancel") } },
        ) { DatePicker(state = state) }
    }
}
