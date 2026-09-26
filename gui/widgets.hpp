#pragma once

// Reusable ImGui widgets for OpenTax: inputs bound directly to model values, a form
// editor generated from a record schema, and a few display helpers.

#include "imgui.h"
#include "opentax/model.hpp"
#include "theme.hpp"

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace otgui::ui {

// ---- inputs bound to model values. Each returns true when the value changed.
bool InputString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0);
bool InputStringHint(const char* label, const char* hint, std::string& value, ImGuiInputTextFlags flags = 0);
// Money typed as "1234.56", "$1,234.56" or blank (zero). Invalid text turns red and the
// value is left unchanged.
bool MoneyInput(const char* label, ot::Money& value, float width = 0.0f);
// A date with a calendar popup; blank clears it.
bool DateInput(const char* label, std::optional<ot::Date>& value, float width = 0.0f);
bool IntInput(const char* label, int& value, int min, int max, float width = 0.0f);

template <class E>
bool ChoiceCombo(const char* label, E& value, float width = 0.0f) {
    const auto& list = ot::choices(value);
    bool changed = false;
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, list[static_cast<std::size_t>(value)].label)) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const bool selected = static_cast<std::size_t>(value) == i;
            if (ImGui::Selectable(list[i].label, selected)) {
                value = static_cast<E>(i);
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Small "(?)" marker with a wrapped tooltip.
void HelpMarker(const char* text);

// ---- schema-driven form editor
// Renders every field of `record` as a labeled row. `hide` can skip fields by key.
// Returns true when anything changed.
template <class T>
bool RecordEditor(const char* id, T& record, const ot::Schema<T>& schema,
                  const std::function<bool(const char* key)>& hide = {}, float labelWidth = 0.0f) {
    bool changed = false;
    const float fs = ImGui::GetFontSize();
    const float lw = labelWidth > 0.0f ? labelWidth : fs * 17.0f;
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, lw);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& f : schema.fields) {
        if (hide && hide(f.key)) continue;
        ImGui::PushID(f.key);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + lw - fs * 1.4f);
        ImGui::TextUnformatted(f.label);
        ImGui::PopTextWrapPos();
        if (*f.help) {
            ImGui::SameLine();
            HelpMarker(f.help);
        }
        ImGui::TableSetColumnIndex(1);
        std::visit(
            [&](auto member) {
                auto& v = record.*member;
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, std::string>) {
                    ImGui::SetNextItemWidth(fs * 18.0f);
                    changed |= InputString("##v", v);
                } else if constexpr (std::is_same_v<V, ot::Money>) {
                    changed |= MoneyInput("##v", v, fs * 10.0f);
                } else if constexpr (std::is_same_v<V, std::optional<ot::Date>>) {
                    changed |= DateInput("##v", v);
                } else if constexpr (std::is_same_v<V, bool>) {
                    changed |= ImGui::Checkbox("##v", &v);
                } else if constexpr (std::is_same_v<V, int>) {
                    changed |= IntInput("##v", v, 0, 100000, fs * 7.0f);
                } else {
                    changed |= ChoiceCombo("##v", v, fs * 22.0f);
                }
            },
            f.member);
        ImGui::PopID();
    }
    ImGui::EndTable();
    return changed;
}

// ---- text
void Heading(const char* text);
void SubHeading(const char* text);
void Muted(const char* text);
void MutedWrapped(const char* text);
void TextRight(const char* text);
void MoneyText(ot::Money amount, bool rightAlign = true, bool redIfNegative = true);
void Badge(const char* text, ImVec4 color);
void ErrorText(const std::string& error);
std::string usd(ot::Money m);

// ---- buttons
bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool DangerButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool LinkButton(const char* label);

// ---- layout pieces
void StatCard(const char* id, const char* title, const std::string& value, ImVec4 valueColor, const std::string& note,
              float width);
// A rounded panel; call EndCard() after the content.
void BeginCard(const char* id, float width = 0.0f, float height = 0.0f);
void EndCard(bool spacing = true);
// A colored note box (info, warning, error) with wrapped text.
void Callout(const char* text, ImVec4 color);

}  // namespace otgui::ui
