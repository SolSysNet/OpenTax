package org.opentax.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import org.opentax.app.engine.Schema
import org.opentax.app.engine.money
import org.opentax.app.engine.usd
import java.math.BigDecimal
import java.time.LocalDate

// ------------------------------------------------------------------ home

@Composable
fun HomeScreen(vm: AppViewModel) {
    val r = vm.result ?: return
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    val first = vm.value("taxpayer", "first")
    ScreenTitle(if (first.isBlank()) "Your ${vm.value("info", "year")} tax return" else "Welcome, $first",
        "Work through each step. Your refund updates as you go, and everything saves automatically.")
    when {
        r.errors > 0 -> Callout("${r.errors} to fix before you file. See Review.", negativeColor)
        r.warnings > 0 -> Callout("${r.warnings} to double-check. See Review.", warningColor)
    }
    val incomeForms = incomeSections.sumOf { vm.doc?.count(it.section) ?: 0 }
    val dependents = vm.doc?.count("dependent") ?: 0
    val status = Schema.instance.section("info").field("status")?.choices?.firstOrNull { it.key == vm.value("info", "status") }?.label.orEmpty()
    val steps = listOf(
        Triple(Screen.AboutYou, "About you", status),
        Triple(Screen.Dependents, "Dependents", if (dependents == 0) "None" else "$dependents ${if (dependents == 1) "dependent" else "dependents"}"),
        Triple(Screen.Income, "Income", "$incomeForms ${if (incomeForms == 1) "form" else "forms"} · total income ${r.s("totalIncome").usd()}"),
        Triple(Screen.Deductions, "Deductions", (if (r.itemized) "Itemized " else "Standard deduction ") + r.s("deduction").usd()),
        Triple(Screen.Credits, "Credits", (r.s("credits") + r.s("refundableCredits")).usd()),
        Triple(Screen.Payments, "Payments", "Withholding and payments " + (r.s("totalPayments") - r.s("refundableCredits")).usd()),
        Triple(Screen.Review, "Review", if (r.errors > 0) "${r.errors} to fix" else if (r.warnings > 0) "${r.warnings} to check" else "Looks good"),
        Triple(Screen.Forms, "Forms & PDF", r.forms.count { !it.worksheet }.let { "$it ${if (it == 1) "form" else "forms and schedules"}" }),
    )
    steps.forEachIndexed { i, (screen, title, detail) ->
        SectionCard {
            Row(Modifier.fillMaxWidth().clickable { vm.step(screen) }, verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("${i + 1}. $title", fontWeight = FontWeight.SemiBold)
                    Muted(detail)
                }
                Text("›", style = MaterialTheme.typography.headlineSmall, color = MaterialTheme.colorScheme.primary)
            }
        }
    }
    StepButtons(null, "Get started") { vm.step(Screen.AboutYou) }
}

// ------------------------------------------------------------------ about you

@Composable
fun AboutYouScreen(vm: AppViewModel) {
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    val year = vm.value("info", "year").toIntOrNull() ?: 2025
    val status = vm.value("info", "status")
    ScreenTitle("About you", "Your filing status and a few details decide your tax rates and standard deduction.")

    SectionCard("Tax year") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            for (y in listOf(2025, 2026)) {
                RadioButton(selected = year == y, onClick = { vm.set("info", "year", y.toString()) })
                Text("$y", modifier = Modifier.padding(end = 16.dp))
            }
        }
        Muted(if (year >= 2026) "Uses 2026 inflation adjustments and draft IRS forms." else "The return you file in 2026.")
    }

    SectionCard("Filing status") {
        val help = mapOf(
            "single" to "Unmarried, divorced, or legally separated on December 31, $year.",
            "mfj" to "Married on December 31 and filing one return together. Usually the lowest total tax.",
            "mfs" to "Married, each filing your own return. Often costs more; several credits aren't allowed.",
            "hoh" to "Unmarried, and you paid more than half the cost of a home for a qualifying person who lived with you.",
            "qss" to "Your spouse died in ${year - 2} or ${year - 1}, you haven't remarried, and a dependent child lives with you.",
        )
        for (choice in Schema.instance.section("info").field("status")!!.choices) {
            Row(Modifier.fillMaxWidth().clickable { vm.set("info", "status", choice.key) }.padding(vertical = 4.dp)) {
                RadioButton(selected = status == choice.key, onClick = { vm.set("info", "status", choice.key) })
                Column(Modifier.padding(top = 12.dp)) {
                    Text(choice.label)
                    Muted(help[choice.key].orEmpty())
                }
            }
        }
        if (status == "mfs") {
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            RecordEditor(vm, "info", hide = { it !in setOf("livedapart", "spouseitemizes", "eicseparated") })
        }
    }

    val personHidden = setOf("tradira", "rothira")
    SectionCard("You") { RecordEditor(vm, "taxpayer", hide = { it in personHidden }) }
    if (status == "mfj" || status == "mfs") {
        SectionCard("Your spouse") {
            RecordEditor(vm, "spouse", hide = { it in personHidden || (status == "mfs" && it !in setOf("first", "last")) })
        }
    }
    SectionCard("Home address") {
        RecordEditor(vm, "info", hide = { it !in setOf("street", "city", "state", "zip") })
    }
    SectionCard("Privacy") {
        Field(vm, "info", "citizen")
    }
    StepButtons({ vm.step(Screen.Home) }, "Continue") { vm.step(Screen.Dependents) }
}

// ------------------------------------------------------------------ lists

data class ListSection(val section: String, val title: String, val blurb: String, val addLabel: String)

val incomeSections = listOf(
    ListSection("w2", "Wages", "Form W-2, tips and overtime", "Add a W-2"),
    ListSection("1099int", "Interest", "Form 1099-INT", "Add a 1099-INT"),
    ListSection("1099div", "Dividends", "Form 1099-DIV", "Add a 1099-DIV"),
    ListSection("capgain", "Investment sales", "Stocks, funds, crypto (1099-B)", "Add a sale"),
    ListSection("business", "Self-employment", "Schedule C, 1099-NEC, 1099-K", "Add a business"),
    ListSection("1099r", "Retirement", "Pensions, IRA, 401(k) (1099-R)", "Add a 1099-R"),
    ListSection("ssa1099", "Social Security", "Form SSA-1099", "Add an SSA-1099"),
    ListSection("1099g", "Unemployment", "Form 1099-G", "Add a 1099-G"),
    ListSection("otherincome", "Other income", "Prizes, jury duty, hobbies", "Add other income"),
)

private val otherSections = listOf(
    ListSection("dependent", "Dependents", "", "Add a dependent"),
    ListSection("education", "Education expenses", "", "Add a student"),
)

fun listSection(section: String): ListSection = (incomeSections + otherSections).first { it.section == section }

/** A short label and an amount for one entry in a list. */
fun describe(vm: AppViewModel, section: String, index: Int): Pair<String, BigDecimal?> {
    val v = vm.doc?.get(section, index) ?: return "" to null
    fun m(key: String) = v[key].orEmpty().money()
    return when (section) {
        "w2" -> v["employer"].orEmpty() to m("wages")
        "1099int" -> v["payer"].orEmpty() to m("interest") + m("usbonds")
        "1099div" -> v["payer"].orEmpty() to m("ordinary")
        "capgain" -> v["desc"].orEmpty() to m("proceeds") - m("basis") + m("adjust")
        "business" -> v["name"].orEmpty() to (vm.result?.form("Schedule C (${v["name"].orEmpty().ifBlank { "Business ${index + 1}" }})")?.line("31")?.amount ?: m("receipts"))
        "1099r" -> v["payer"].orEmpty() to m("gross")
        "ssa1099" -> (if (v["owner"] == "spouse") "Spouse's benefits" else "Your benefits") to m("benefits")
        "1099g" -> v["payer"].orEmpty() to m("comp")
        "otherincome" -> v["desc"].orEmpty() to m("amount")
        "dependent" -> {
            val name = listOf(v["first"].orEmpty(), v["last"].orEmpty()).joinToString(" ").trim()
            val dob = runCatching { LocalDate.parse(v["dob"].orEmpty()) }.getOrNull()
            val year = vm.value("info", "year").toIntOrNull() ?: 2025
            (name + (if (dob != null) " · age ${year - dob.year}" else "")) to null
        }
        "education" -> v["student"].orEmpty() to m("expenses")
        else -> "" to null
    }
}

@Composable
fun ListScreen(vm: AppViewModel, section: String, intro: String) {
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    val info = listSection(section)
    val title = Schema.instance.section(section).title
    ScreenTitle(if (section in incomeSections.map { it.section }) "${info.title} ($title)" else info.title, intro)
    val count = vm.doc?.count(section) ?: 0
    if (count == 0) Muted("Nothing entered yet.")
    for (i in 0 until count) {
        val (label, amount) = describe(vm, section, i)
        SectionCard {
            Row(Modifier.fillMaxWidth().clickable { vm.open(Screen.Editor(section, i)) }, verticalAlignment = Alignment.CenterVertically) {
                Text(label.ifBlank { "$title ${i + 1}" }, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                if (amount != null) Text(amount.usd())
                Text("  ›", color = MaterialTheme.colorScheme.primary)
            }
        }
    }
    Spacer(Modifier.height(8.dp))
    Button(onClick = { vm.open(Screen.Editor(section, vm.add(section))) }) { Text(info.addLabel) }
    Spacer(Modifier.height(24.dp))
}

@Composable
fun EditorScreen(vm: AppViewModel, section: String, index: Int) {
    val count = vm.doc?.count(section) ?: 0
    if (index >= count) return
    var confirmDelete by remember { mutableStateOf(false) }
    val spec = Schema.instance.section(section)
    ScreenTitle(spec.title, "Enter the amounts exactly as they appear on your form. Leave empty boxes blank.")
    SectionCard { RecordEditor(vm, section, index) }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.padding(top = 8.dp)) {
        Button(onClick = { vm.back() }, modifier = Modifier.weight(1f)) { Text("Done") }
        OutlinedButton(onClick = { confirmDelete = true }, modifier = Modifier.weight(1f)) { Text("Delete", color = negativeColor) }
    }
    Spacer(Modifier.height(24.dp))
    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete this entry?") },
            text = { Text("This removes the ${spec.title} from your return.") },
            confirmButton = {
                TextButton(onClick = {
                    confirmDelete = false
                    vm.back()
                    vm.remove(section, index)
                }) { Text("Delete", color = negativeColor) }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("Cancel") } },
        )
    }
}

@Composable
fun DependentsScreen(vm: AppViewModel) {
    ListScreen(vm, "dependent",
        "Children and relatives you support. Each qualifying child under 17 is worth up to $2,200 (child tax credit); other dependents up to $500.")
    StepButtons({ vm.step(Screen.AboutYou) }, "Continue") { vm.step(Screen.Income) }
}

@Composable
fun IncomeScreen(vm: AppViewModel) {
    val r = vm.result ?: return
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    ScreenTitle("Income", "Choose the kinds of income you had. Most people only need W-2s.")
    for (s in incomeSections) {
        val count = vm.doc?.count(s.section) ?: 0
        val total = (0 until count).fold(BigDecimal.ZERO) { acc, i -> acc + (describe(vm, s.section, i).second ?: BigDecimal.ZERO) }
        SectionCard {
            Row(Modifier.fillMaxWidth().clickable { vm.open(Screen.IncomeList(s.section)) }, verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(s.title, fontWeight = FontWeight.SemiBold)
                    Muted(if (count > 0) "$count entered · ${total.usd()}" else s.blurb)
                }
                Text(if (count > 0) "Review ›" else "Add ›", color = MaterialTheme.colorScheme.primary)
            }
        }
    }
    MoneyRow("Total income", r.s("totalIncome"), bold = true)
    StepButtons({ vm.step(Screen.Dependents) }, "Continue") { vm.step(Screen.Deductions) }
}

private val incomeIntros = mapOf(
    "w2" to "Enter each W-2. Box 12 deferrals and box 13 affect your IRA deduction and saver's credit. Enter qualified tips and overtime to deduct them on Schedule 1-A.",
    "1099int" to "Bank, savings bond and bond interest.",
    "1099div" to "Dividends and fund capital gain distributions. Qualified dividends are taxed at lower rates.",
    "capgain" to "Stocks, bonds, funds and crypto you sold. Losses offset gains, and up to $3,000 of net loss offsets other income.",
    "business" to "Freelance, contract and gig work, including 1099-NEC and 1099-K. Profit is subject to self-employment tax.",
    "1099r" to "Pensions, annuities, and IRA or 401(k) distributions.",
    "ssa1099" to "Up to 85% of benefits can be taxable, depending on your other income.",
    "1099g" to "Unemployment compensation is fully taxable.",
    "otherincome" to "Prizes, jury duty pay, hobby income and other taxable income.",
)

@Composable
fun IncomeListScreen(vm: AppViewModel, section: String) = ListScreen(vm, section, incomeIntros[section].orEmpty())

// ------------------------------------------------------------------ deductions

@Composable
fun DeductionsScreen(vm: AppViewModel) {
    val r = vm.result ?: return
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    val status = vm.value("info", "status")
    val year = vm.value("info", "year").toIntOrNull() ?: 2025
    ScreenTitle("Deductions", "Adjustments reduce your income directly. Then you get the larger of the standard deduction or your itemized deductions.")

    val standard = r.s("standardDeduction")
    val itemized = r.s("itemizedDeduction")
    Callout(
        when {
            itemized.signum() == 0 -> "You're getting the ${standard.usd()} standard deduction. Enter itemized deductions below only if they might add up to more."
            r.itemized -> "Itemizing saves you more: ${itemized.usd()} itemized vs. the ${standard.usd()} standard deduction."
            else -> "The standard deduction (${standard.usd()}) is more than your itemized deductions (${itemized.usd()}), so OpenTax uses it."
        } + if (r.s("nonItemizerCharity").signum() > 0) " You also get ${r.s("nonItemizerCharity").usd()} for cash gifts to charity (line 12f)." else "",
        MaterialTheme.colorScheme.primary,
    )

    SectionCard("Schedule 1-A: tips, overtime, car loan and seniors") {
        val w2s = vm.doc?.count("w2") ?: 0
        var tips = BigDecimal.ZERO
        var overtime = BigDecimal.ZERO
        for (i in 0 until w2s) {
            tips += vm.value("w2", "tips", i).money()
            overtime += vm.value("w2", "overtime", i).money()
        }
        MoneyRow("Qualified tips on your W-2s", tips)
        MoneyRow("Qualified overtime on your W-2s", overtime)
        TextButton(onClick = { vm.step(Screen.Income); vm.open(Screen.IncomeList("w2")) }) { Text("Enter tips and overtime on your W-2s") }
        Field(vm, "adjustments", "carloan")
        MoneyRow("Enhanced deduction for seniors", r.s("seniorDeduction"), note = "Automatic for anyone born before January 2, ${year - 64}")
        HorizontalDivider(Modifier.padding(vertical = 6.dp))
        MoneyRow("Total Schedule 1-A deductions", r.s("schedule1A"), bold = true)
    }

    SectionCard("Adjustments to income") {
        RecordEditor(vm, "adjustments", hide = { it == "carloan" || (it == "educatorspouse" && status != "mfj") })
        Field(vm, "taxpayer", "tradira", label = "Traditional IRA contributions (you)")
        if (status == "mfj") Field(vm, "spouse", "tradira", label = "Traditional IRA contributions (spouse)")
        HorizontalDivider(Modifier.padding(vertical = 6.dp))
        MoneyRow("Total adjustments", r.s("adjustments"), bold = true)
    }

    var showItemized by remember { mutableStateOf(r.itemized || itemized.signum() > 0) }
    SectionCard("Itemized deductions (Schedule A)") {
        if (!showItemized) {
            Muted("Mortgage interest, state and local taxes, charity, large medical bills.")
            TextButton(onClick = { showItemized = true }) { Text("Enter itemized deductions") }
        } else {
            Muted("State income tax withheld on your W-2s is included automatically." +
                if (year >= 2026) " From 2026, gifts to charity count only above 0.5% of AGI." else "")
            Spacer(Modifier.height(8.dp))
            val sales = vm.value("itemized", "usesales") == "yes"
            RecordEditor(vm, "itemized", hide = { (it == "salestax" && !sales) || (it == "stateincome" && sales) })
            Field(vm, "info", "forceitemize")
            HorizontalDivider(Modifier.padding(vertical = 6.dp))
            MoneyRow("Total itemized deductions", itemized, bold = true)
        }
    }
    StepButtons({ vm.step(Screen.Income) }, "Continue") { vm.step(Screen.Credits) }
}

// ------------------------------------------------------------------ credits

@Composable
fun CreditsScreen(vm: AppViewModel) {
    val r = vm.result ?: return
    @Suppress("UNUSED_VARIABLE") val revision = vm.revision
    ScreenTitle("Credits", "Credits reduce your tax dollar for dollar. Refundable credits can be paid to you even if you owe no tax.")
    SectionCard("Your credits") {
        val rows = listOf(
            Triple("Child tax credit / other dependents", "1040", "19"),
            Triple("Additional child tax credit (refundable)", "1040", "28"),
            Triple("Earned income credit (refundable)", "1040", "27a"),
            Triple("Child and dependent care credit", "Schedule 3", "2"),
            Triple("Education credits", "Schedule 3", "3"),
            Triple("American opportunity credit (refundable)", "1040", "29"),
            Triple("Retirement savings contributions credit", "Schedule 3", "4"),
        )
        var total = BigDecimal.ZERO
        for ((label, form, line) in rows) {
            val amount = r.line(form, line)
            total += amount
            MoneyRow(label, amount, note = r.form(form)?.line(line)?.how?.takeIf { it.isNotEmpty() && amount.signum() != 0 })
        }
        HorizontalDivider(Modifier.padding(vertical = 6.dp))
        MoneyRow("Total", total, bold = true)
        Muted("Child credits and the EIC come from your dependents and income; there's nothing extra to enter.")
    }

    SectionCard("Child and dependent care") {
        Muted("What you paid for care of a child under 13 (or a disabled dependent) so you could work: daycare, preschool, after-school care, day camp.")
        val count = vm.doc?.count("dependent") ?: 0
        if (count == 0) Muted("Add dependents first.")
        for (i in 0 until count) {
            Field(vm, "dependent", "care", i, label = "Care for ${describe(vm, "dependent", i).first.ifBlank { "dependent ${i + 1}" }}")
        }
    }

    SectionCard("Education (Form 1098-T)") {
        Muted("College tuition and fees, minus tax-free scholarships. The American opportunity credit is worth up to $2,500 per student.")
        val count = vm.doc?.count("education") ?: 0
        for (i in 0 until count) {
            val (label, amount) = describe(vm, "education", i)
            MoneyRow(label.ifBlank { "Student ${i + 1}" }, amount ?: BigDecimal.ZERO) { vm.open(Screen.Editor("education", i)) }
        }
        TextButton(onClick = { vm.open(Screen.Editor("education", vm.add("education"))) }) { Text("Add a student") }
    }

    SectionCard("Retirement savings contributions credit") {
        Muted("For lower and moderate incomes. Traditional IRA contributions and W-2 box 12 deferrals count automatically; add Roth IRA contributions here.")
        Field(vm, "taxpayer", "rothira", label = "Roth IRA contributions (you)")
        if (vm.value("info", "status") == "mfj") Field(vm, "spouse", "rothira", label = "Roth IRA contributions (spouse)")
    }
    StepButtons({ vm.step(Screen.Deductions) }, "Continue") { vm.step(Screen.Payments) }
}

// ------------------------------------------------------------------ payments

@Composable
fun PaymentsScreen(vm: AppViewModel) {
    val r = vm.result ?: return
    ScreenTitle("Payments and carryovers", "Tax you've already paid, and amounts carried over from last year's return.")
    SectionCard("Withholding") {
        MoneyRow("From W-2s (line 25a)", r.line("1040", "25a"))
        MoneyRow("From 1099s (line 25b)", r.line("1040", "25b"))
        MoneyRow("Additional Medicare Tax withholding (line 25c)", r.line("1040", "25c"))
        Muted("Withholding comes from the forms you entered under Income.")
    }
    SectionCard("Estimated and other payments") { RecordEditor(vm, "payments") }
    SectionCard("Carryovers from last year") { RecordEditor(vm, "carryovers") }
    StepButtons({ vm.step(Screen.Credits) }, "Review") { vm.step(Screen.Review) }
}
