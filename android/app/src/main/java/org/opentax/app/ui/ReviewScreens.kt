package org.opentax.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import org.opentax.app.engine.Severity
import org.opentax.app.engine.usd

/** Where a review note's topic is fixed. */
private fun stepForTopic(topic: String): Screen? {
    val t = topic.lowercase()
    val map = listOf(
        "about you" to Screen.AboutYou, "filing status" to Screen.AboutYou, "dependent care" to Screen.Credits,
        "dependent" to Screen.Dependents, "w-2" to Screen.Income, "1099" to Screen.Income, "schedule c" to Screen.Income,
        "self-employment tax" to Screen.Income, "social security tax" to Screen.Income, "qbi" to Screen.Income,
        "capital losses" to Screen.Payments, "ira" to Screen.Deductions, "student loan" to Screen.Deductions,
        "self-employed health" to Screen.Deductions, "investment interest" to Screen.Deductions, "charitable" to Screen.Deductions,
        "no tax on" to Screen.Deductions, "senior" to Screen.Deductions, "itemized" to Screen.Deductions,
        "mortgage" to Screen.Deductions, "education" to Screen.Credits, "saver" to Screen.Credits,
        "earned income" to Screen.Credits, "refundable" to Screen.AboutYou, "estimated tax" to Screen.Payments,
    )
    return map.firstOrNull { t.contains(it.first) }?.second
}

@Composable
fun ReviewScreen(vm: AppViewModel, onSavePdf: () -> Unit) {
    val r = vm.result ?: return
    val year = vm.value("info", "year").toIntOrNull() ?: 2025
    ScreenTitle("Review your return", "Fix anything marked as an error, look over the checks, then save your forms.")
    if (r.errors > 0) Callout("${r.errors} ${if (r.errors == 1) "error needs" else "errors need"} fixing before this return is ready to file.", negativeColor)
    else Callout("No errors found. Review the notes below and your forms before you file.", positiveColor)

    SectionCard("Things to review") {
        for (sev in listOf(Severity.ERROR, Severity.WARNING, Severity.INFO)) {
            for (d in r.diagnostics.filter { it.severity == sev }) {
                val (tag, color) = when (sev) {
                    Severity.ERROR -> "Error" to negativeColor
                    Severity.WARNING -> "Check" to warningColor
                    Severity.INFO -> "Note" to MaterialTheme.colorScheme.onSurfaceVariant
                }
                Row(Modifier.padding(vertical = 6.dp)) {
                    Text(tag, color = color, fontWeight = FontWeight.Bold, modifier = Modifier.width(52.dp))
                    Column {
                        Text(d.topic, fontWeight = FontWeight.SemiBold)
                        Text(d.message, style = MaterialTheme.typography.bodyMedium)
                        stepForTopic(d.topic)?.let { target -> TextButton(onClick = { vm.step(target) }) { Text("Go there") } }
                    }
                }
            }
        }
    }

    SectionCard("Your $year tax summary") {
        val ids = r.lineIds
        val rows = listOf(
            Triple("Total income", r.s("totalIncome"), false),
            Triple("Adjustments", r.s("adjustments").negate(), false),
            Triple("Adjusted gross income", r.s("agi"), true),
            Triple(if (r.itemized) "Itemized deductions" else "Standard deduction", r.s("deduction").negate(), false),
            Triple("Charitable deduction (line ${ids["charity"]})", r.s("nonItemizerCharity").negate(), false),
            Triple("Schedule 1-A deductions", r.s("schedule1A").negate(), false),
            Triple("QBI deduction", r.s("qbiDeduction").negate(), false),
            Triple("Taxable income", r.s("taxableIncome"), true),
            Triple("Income tax", r.s("incomeTax"), false),
            Triple("Alternative minimum tax", r.s("amt"), false),
            Triple("Nonrefundable credits", r.s("credits").negate(), false),
            Triple("Other taxes", r.s("otherTaxes"), false),
            Triple("Total tax (line ${ids["totalTax"]})", r.s("totalTax"), true),
            Triple("Withholding", r.s("withholding").negate(), false),
            Triple("Estimated and other payments", r.s("estimatedPayments").negate(), false),
            Triple("Refundable credits", r.s("refundableCredits").negate(), false),
        )
        for ((label, amount, bold) in rows) {
            if (amount.signum() == 0 && !bold) continue
            MoneyRow(label, amount, bold)
        }
        HorizontalDivider(Modifier.padding(vertical = 6.dp))
        val refund = r.s("refund")
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(
                if (refund.signum() > 0) "Your refund" else if (r.s("owed").signum() > 0) "Amount you owe" else "Balance",
                style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold,
                color = if (refund.signum() > 0) positiveColor else if (r.s("owed").signum() > 0) negativeColor else Color.Unspecified,
                modifier = Modifier.weight(1f),
            )
            Text((if (refund.signum() > 0) refund else r.s("owed")).usd(), style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
        }
    }

    SectionCard("How to file") {
        Text("Forms on your return: " + r.forms.filter { !it.worksheet }.joinToString(", ") { it.id })
        Spacer(Modifier.height(8.dp))
        Text("1. Save the PDF. It has every form, schedule and worksheet, line by line, with explanations.")
        Text("2. Copy the amounts onto the official $year IRS forms: IRS Free File Fillable Forms, or paper forms from irs.gov.")
        Text("3. Sign and file. $year returns are due April 15, ${year + 1}; with an extension, October 15, ${year + 1}.")
        Spacer(Modifier.height(4.dp))
        Muted("OpenTax doesn't e-file and doesn't compute state returns or the underpayment penalty (Form 2210).")
        Row(Modifier.padding(top = 12.dp)) {
            Button(onClick = onSavePdf, modifier = Modifier.weight(1f)) { Text("Save PDF") }
            Spacer(Modifier.width(12.dp))
            OutlinedButton(onClick = { vm.step(Screen.Forms) }, modifier = Modifier.weight(1f)) { Text("View forms") }
        }
    }
    Spacer(Modifier.height(24.dp))
}

@Composable
fun FormsScreen(vm: AppViewModel) {
    val r = vm.result ?: return
    ScreenTitle("Forms and worksheets", "Every line OpenTax figured. Open a form and tap a line to see how it was calculated.")
    for (worksheets in listOf(false, true)) {
        Text(if (worksheets) "WORKSHEETS" else "FORMS TO FILE", style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 12.dp, bottom = 4.dp))
        for (f in r.forms.filter { it.worksheet == worksheets }) {
            SectionCard {
                Row(Modifier.fillMaxWidth().clickable { vm.open(Screen.Form(f.id)) }, verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(f.id, fontWeight = FontWeight.SemiBold)
                        Muted(f.title)
                    }
                    Text("›", style = MaterialTheme.typography.headlineSmall, color = MaterialTheme.colorScheme.primary)
                }
            }
        }
    }
    Spacer(Modifier.height(24.dp))
}

@Composable
fun FormScreen(vm: AppViewModel, id: String) {
    val form = vm.result?.form(id) ?: return
    var open by remember(id) { mutableStateOf<String?>(null) }
    ScreenTitle(form.id, form.title + if (form.worksheet) ". Worksheet: keep for your records." else "")
    SectionCard {
        form.lines.forEachIndexed { i, line ->
            if (i > 0) HorizontalDivider()
            Column(Modifier.fillMaxWidth().clickable(enabled = line.how.isNotEmpty()) { open = if (open == line.number) null else line.number }
                .padding(vertical = 8.dp)) {
                Row(verticalAlignment = Alignment.Top) {
                    Text(line.number, fontWeight = FontWeight.Bold, modifier = Modifier.width(44.dp))
                    Text(line.label, modifier = Modifier.weight(1f).padding(end = 8.dp))
                    Text(line.display, color = if (line.amount.signum() < 0) negativeColor else Color.Unspecified)
                }
                if (open == line.number) {
                    Text(line.how, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.padding(start = 44.dp, top = 4.dp))
                }
            }
        }
    }
    Spacer(Modifier.height(24.dp))
}
