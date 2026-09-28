package org.opentax.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import org.opentax.app.engine.CalcResult
import org.opentax.app.engine.usd
import java.math.BigDecimal
import java.math.RoundingMode

// ------------------------------------------------------------------ theme

private val Blue = Color(0xFF1E6ED2)

val positiveColor: Color @Composable get() = if (isSystemInDarkTheme()) Color(0xFF6EC878) else Color(0xFF1E8228)
val negativeColor: Color @Composable get() = if (isSystemInDarkTheme()) Color(0xFFF06E64) else Color(0xFFC82D28)
val warningColor: Color @Composable get() = if (isSystemInDarkTheme()) Color(0xFFEBB450) else Color(0xFFA06400)

@Composable
fun OpenTaxTheme(content: @Composable () -> Unit) {
    val colors = if (isSystemInDarkTheme()) {
        darkColorScheme(primary = Color(0xFF589CF0), onPrimary = Color.White, background = Color(0xFF1E2024), surface = Color(0xFF1E2024))
    } else {
        lightColorScheme(primary = Blue, onPrimary = Color.White, background = Color(0xFFF7F8FA), surface = Color(0xFFF7F8FA))
    }
    MaterialTheme(colorScheme = colors, content = content)
}

// ------------------------------------------------------------------ pieces

/** Refund or balance due, pinned under the top bar, with the main figures. */
@Composable
fun RefundBar(result: CalcResult) {
    val refund = result.s("refund")
    val owed = result.s("owed")
    val tint = when {
        refund.signum() > 0 -> positiveColor
        owed.signum() > 0 -> negativeColor
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }
    Surface(color = tint.copy(alpha = 0.10f), modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.padding(horizontal = 16.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    when {
                        refund.signum() > 0 -> "Federal refund"
                        owed.signum() > 0 -> "Federal tax due"
                        else -> "Federal balance"
                    },
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    (if (refund.signum() > 0) refund else owed).usd(),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold,
                    color = tint,
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                SmallStat("Total tax", result.s("totalTax").usd())
                SmallStat("Tax rate", "${effectiveRate(result)} effective · ${result.marginalPercent}% marginal")
            }
        }
    }
}

@Composable
private fun SmallStat(label: String, value: String) {
    Row {
        Text("$label  ", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.SemiBold)
    }
}

fun effectiveRate(result: CalcResult): String {
    val agi = result.s("agi")
    val tax = result.s("totalTax")
    if (agi.signum() <= 0 || tax.signum() <= 0) return "0%"
    return tax.multiply(BigDecimal(100)).divide(agi, 1, RoundingMode.HALF_UP).toPlainString() + "%"
}

@Composable
fun SectionCard(title: String? = null, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow),
    ) {
        Column(Modifier.padding(16.dp)) {
            if (title != null) {
                Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(8.dp))
            }
            content()
        }
    }
}

@Composable
fun ScreenTitle(title: String, subtitle: String? = null) {
    Text(title, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
    if (subtitle != null) {
        Spacer(Modifier.height(4.dp))
        Muted(subtitle)
    }
    Spacer(Modifier.height(12.dp))
}

@Composable
fun Muted(text: String, modifier: Modifier = Modifier) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier)
}

@Composable
fun Callout(text: String, color: Color) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 6.dp).height(IntrinsicSize.Min)
            .background(color.copy(alpha = 0.12f), RoundedCornerShape(8.dp)),
    ) {
        Box(Modifier.width(4.dp).fillMaxHeight().background(color))
        Text(text, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(12.dp))
    }
}

@Composable
fun MoneyRow(label: String, amount: BigDecimal, bold: Boolean = false, note: String? = null, onClick: (() -> Unit)? = null) {
    Row(
        Modifier.fillMaxWidth().then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier).padding(vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal)
            if (note != null) Muted(note)
        }
        Text(
            amount.usd(),
            fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal,
            color = if (amount.signum() < 0) negativeColor else Color.Unspecified,
            textAlign = TextAlign.End,
        )
    }
}

/** Back / Continue buttons at the bottom of an interview step. */
@Composable
fun StepButtons(onBack: (() -> Unit)?, next: String?, onNext: (() -> Unit)?) {
    Spacer(Modifier.height(16.dp))
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        if (onBack != null) OutlinedButton(onClick = onBack, modifier = Modifier.weight(1f)) { Text("Back") }
        if (next != null && onNext != null) Button(onClick = onNext, modifier = Modifier.weight(1f)) { Text(next) }
    }
    Spacer(Modifier.height(24.dp))
}
