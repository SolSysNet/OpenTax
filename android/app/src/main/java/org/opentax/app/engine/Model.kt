package org.opentax.app.engine

import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.text.DecimalFormat
import java.text.DecimalFormatSymbols
import java.util.Locale

// ------------------------------------------------------------------ schema

enum class FieldKind { TEXT, MONEY, DATE, BOOL, INT, CHOICE }

data class Choice(val key: String, val label: String)

data class FieldSpec(
    val key: String,
    val label: String,
    val help: String,
    val kind: FieldKind,
    val choices: List<Choice>,
)

data class SectionSpec(val section: String, val title: String, val fields: List<FieldSpec>) {
    fun field(key: String): FieldSpec? = fields.firstOrNull { it.key == key }
}

/** Every record type and field, as declared once in the engine's schema. */
class Schema private constructor(val singles: List<SectionSpec>, val lists: List<SectionSpec>) {
    fun section(name: String): SectionSpec =
        (singles + lists).firstOrNull { it.section == name } ?: error("unknown section $name")

    companion object {
        val instance: Schema by lazy { parse(Native.schema()) }

        private fun parse(json: String): Schema {
            val root = JSONObject(json)
            fun sections(array: JSONArray) = (0 until array.length()).map { i ->
                val s = array.getJSONObject(i)
                val fields = s.getJSONArray("fields")
                SectionSpec(
                    s.getString("section"),
                    s.getString("title"),
                    (0 until fields.length()).map { j ->
                        val f = fields.getJSONObject(j)
                        val choices = f.getJSONArray("choices")
                        FieldSpec(
                            key = f.getString("key"),
                            label = f.getString("label"),
                            help = f.getString("help"),
                            kind = FieldKind.valueOf(f.getString("kind").uppercase(Locale.ROOT)),
                            choices = (0 until choices.length()).map { k ->
                                Choice(choices.getJSONObject(k).getString("key"), choices.getJSONObject(k).getString("label"))
                            },
                        )
                    },
                )
            }
            return Schema(sections(root.getJSONArray("singles")), sections(root.getJSONArray("lists")))
        }
    }
}

// ------------------------------------------------------------------ results

data class LineView(val number: String, val label: String, val amount: BigDecimal, val text: String, val how: String) {
    val display: String get() = text.ifEmpty { amount.usd() }
}

data class FormView(val id: String, val title: String, val worksheet: Boolean, val lines: List<LineView>) {
    fun line(number: String): LineView? = lines.firstOrNull { it.number == number }
}

enum class Severity { ERROR, WARNING, INFO }

data class Diagnostic(val severity: Severity, val topic: String, val message: String)

/** The calculated return, as produced by the engine's calculate(). */
class CalcResult(
    val summary: Map<String, BigDecimal>,
    val itemized: Boolean,
    val marginalPercent: String,
    val lineIds: Map<String, String>,
    val forms: List<FormView>,
    val diagnostics: List<Diagnostic>,
) {
    fun s(key: String): BigDecimal = summary[key] ?: BigDecimal.ZERO
    fun form(id: String): FormView? = forms.firstOrNull { it.id == id }
    fun line(formId: String, number: String): BigDecimal = form(formId)?.line(number)?.amount ?: BigDecimal.ZERO
    val errors: Int get() = diagnostics.count { it.severity == Severity.ERROR }
    val warnings: Int get() = diagnostics.count { it.severity == Severity.WARNING }

    companion object {
        fun parse(json: String): CalcResult {
            val root = JSONObject(json)
            val summaryJson = root.getJSONObject("summary")
            val summary = summaryJson.keys().asSequence().filter { it != "itemized" }
                .associateWith { BigDecimal(summaryJson.getString(it)) }
            val ids = root.getJSONObject("lineIds")
            val formsJson = root.getJSONArray("forms")
            val forms = (0 until formsJson.length()).map { i ->
                val f = formsJson.getJSONObject(i)
                val lines = f.getJSONArray("lines")
                FormView(
                    f.getString("id"),
                    f.getString("title"),
                    f.getBoolean("worksheet"),
                    (0 until lines.length()).map { j ->
                        val l = lines.getJSONObject(j)
                        LineView(l.getString("n"), l.getString("label"), BigDecimal(l.getString("amount")), l.getString("text"), l.getString("how"))
                    },
                )
            }
            val diagJson = root.getJSONArray("diagnostics")
            val diagnostics = (0 until diagJson.length()).map { i ->
                val d = diagJson.getJSONObject(i)
                Diagnostic(Severity.valueOf(d.getString("severity").uppercase(Locale.ROOT)), d.getString("topic"), d.getString("message"))
            }
            return CalcResult(
                summary = summary,
                itemized = summaryJson.getBoolean("itemized"),
                marginalPercent = root.getString("marginal"),
                lineIds = ids.keys().asSequence().associateWith { ids.getString(it) },
                forms = forms,
                diagnostics = diagnostics,
            )
        }
    }
}

// ------------------------------------------------------------------ money

private val usdFormat = DecimalFormat("#,##0.00", DecimalFormatSymbols(Locale.US))

/** "$1,234.56" or "-$1,234.56". */
fun BigDecimal.usd(): String = if (signum() < 0) "-$" + usdFormat.format(negate()) else "$" + usdFormat.format(this)

/** A field's stored text as a number (blank is zero). */
fun String.money(): BigDecimal = trim().toBigDecimalOrNull() ?: BigDecimal.ZERO
